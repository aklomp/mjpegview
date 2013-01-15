#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mjv_log.c"
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_framerate.h"
#include "mjv_filename.h"
#include "mjv_grabber.h"

// This is a really simple framegrabber for mjpeg streams. It is intended to
// compile with the least possible dependencies, to make it useful on headless,
// underpowered systems. It also keeps the rest of the modules "honest" in
// terms of simple interfaces and modularity.

static int n_frames = 0;
static int read_fd = -1;
static int write_fd = -1;

static bool
copy_string (const char *const src, char **const dst)
{
	size_t len = strlen(src) + 1;
	if ((*dst = malloc(len)) == NULL) {
		log_error("Error: out of memory\n");
		return false;
	}
	memcpy(*dst, src, len);
	return true;
}

static bool
process_cmdline (int argc, char **argv, char **name, char **filename, int *usec, char **host, char **path, char **user, char **pass, int *port)
{
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{ "debug", 0, 0, 'd' },
		{ "filename", 1, 0, 'f' },
		{ "help", 0, 0, 'h' },
		{ "host", 1, 0, 'H' },
		{ "name", 1, 0, 'n' },
		{ "msec", 1, 0, 'm' },
		{ "user", 1, 0, 'u' },
		{ "pass", 1, 0, 'p' },
		{ "path", 1, 0, 'P' },
		{ "port", 1, 0, 'q' },
		{ 0, 0, 0, 0 }
	};
	for (;;) {
		if ((c = getopt_long(argc, argv, "df:hH:n:m:u:p:P:q:", long_options, &option_index)) == -1) {
			break;
		}
		switch (c)
		{
			case 'd': log_debug_on(); break;
			case 'h': break;
			case 'm': *usec = atoi(optarg); break;
			case 'q': *port = atoi(optarg); break;
			case 'f': if (copy_string(optarg, filename)) break; return false;
			case 'H': if (copy_string(optarg, host)) break; return false;
			case 'n': if (copy_string(optarg, name)) break; return false;
			case 'u': if (copy_string(optarg, user)) break; return false;
			case 'p': if (copy_string(optarg, pass)) break; return false;
			case 'P': if (copy_string(optarg, path)) break; return false;
		}
	}
	return true;
}

static void
timestamp_file (const char *const filename, const struct timespec *const timestamp)
{
	struct timespec times[2];

	times[0].tv_sec = timestamp->tv_sec;
	times[0].tv_nsec = timestamp->tv_nsec;

	times[1].tv_sec = timestamp->tv_sec;
	times[1].tv_nsec = timestamp->tv_nsec;

	utimensat(AT_FDCWD, filename, times, 0);
}

static void
print_info (unsigned int framenum, float fps)
{
	int start;
	char buf[50];
	static int first = 1;

	if (first == 0)
	{
		// Move cursor one line upwards:
		buf[0] = 033;
		buf[1] = '[';
		buf[2] = '1';
		buf[3] = 'F';

		// Clear entire line:
		buf[4] = 033;
		buf[5] = '[';
		buf[6] = '2';
		buf[7] = 'K';

		start = 8;
	}
	else {
		first = 0;
		start = 0;
	}
	size_t len = (fps < 0.0)
	    ? start + snprintf(buf + start, sizeof(buf) - start, "Frame %d, (stalled)\n", framenum)
	    : start + snprintf(buf + start, sizeof(buf) - start, "Frame %d, %0.2f fps\n", framenum, fps);

	if (len > sizeof(buf)) {
	       len = sizeof(buf);
	}
	write(STDOUT_FILENO, buf, len);
	fsync(STDOUT_FILENO);
}

static void
write_image_file (char *data, size_t nbytes, char *const srcname, unsigned int framenum, const struct timespec *const timestamp)
{
	FILE *fp;
	char *filename;

	if (data == NULL || nbytes == 0) {
		log_error("Error: frame contains no data\n");
		return;
	}
	if (framenum >= 1000000000) {
		log_error("Error: framenum too large (over 9 digits)\n");
		return;
	}
	// TODO: let user specify the pattern:
	if ((filename = mjv_filename_forge(srcname, framenum, "%f.jpg")) == NULL) {
		log_error("Error: could not forge filename\n");
		return;
	}
	if ((fp = fopen(filename, "w")) == NULL) {
		perror("fopen");
	}
	else {
		log_debug("writing %s\n", filename);
		fwrite(data, nbytes, 1, fp);
		fclose(fp);
		timestamp_file(filename, timestamp);
	}
	free(filename);
}

static void
got_frame_callback (struct mjv_frame *f, void *data)
{
	struct mjv_framerate *fr = data;

	n_frames++;
	log_debug("got frame %d\n", n_frames);

	// Feed the framerate estimator, get estimate:
	mjv_framerate_insert_datapoint(fr, mjv_frame_get_timestamp(f));
	print_info(n_frames, mjv_framerate_estimate(fr));

	// Write to file:
	write_image_file((char *)mjv_frame_get_rawbits(f), mjv_frame_get_num_rawbits(f), NULL, n_frames, mjv_frame_get_timestamp(f));

	// We are responsible for freeing the mjv_frame when we're done with it:
	mjv_frame_destroy(&f);
}

static bool
make_selfpipe_pair (void)
{
	int pfd[2];
	int flags;

	if (pipe(pfd) == -1) {
		return false;
	}
	// Make nonblocking:
	if ((flags = fcntl(pfd[0], F_GETFL)) == -1 || fcntl(pfd[0], F_SETFL, flags | O_NONBLOCK) == -1
	 || (flags = fcntl(pfd[1], F_GETFL)) == -1 || fcntl(pfd[1], F_SETFL, flags | O_NONBLOCK) == -1) {
		close(pfd[0]);
		close(pfd[1]);
		return false;
	}
	read_fd = pfd[0];
	write_fd = pfd[1];
	return true;
}

static void
sig_handler (int signum, siginfo_t *info, void *ptr)
{
	(void)signum;
	(void)info;
	(void)ptr;

	// Terminate a thread by sending a byte of data through the write end
	// of the self-pipe. The grabber catches this and exits gracefully:
	if (write_fd < 0) {
		return;
	}
	while (write(write_fd, "X", 1) == -1 && errno == EAGAIN) {
		continue;
	}
	close(write_fd);
	write_fd = -1;
}

static void
sig_setup (void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));

	act.sa_sigaction = sig_handler;
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
}

int
main (int argc, char **argv)
{
	int ret = 0;
	char *filename = NULL;
	char *name = NULL;
	char *host = NULL;
	char *path = NULL;
	char *user = NULL;
	char *pass = NULL;
	int port = 0;
	int usec = 100;
	struct mjv_source *s = NULL;
	struct mjv_grabber *g = NULL;
	struct mjv_framerate *fr = NULL;

	if (!process_cmdline(argc, argv, &name, &filename, &usec, &host, &path, &user, &pass, &port)) {
		ret = 1;
		goto exit;
	}
	if (filename != NULL) {
		if ((s = mjv_source_create_from_file(name, filename, usec)) == NULL) {
			log_error("Error: could not create source from file\n");
			ret = 1;
			goto exit;
		}
	}
	else if (host != NULL) {
		if ((s = mjv_source_create_from_network(name, host, path, user, pass, port)) == NULL) {
			log_error("Error: could not create source from network\n");
			ret = 1;
			goto exit;
		}
	}
	else {
		log_error("Error: no filename or network definition given\n");
		ret = 1;
		goto exit;
	}
	if ((g = mjv_grabber_create(s)) == NULL) {
		log_error("Error: could not create grabber\n");	// TODO: non-descriptive error messages...
		ret = 1;
		goto exit;
	}
	if ((fr = mjv_framerate_create()) == NULL) {
		log_error("Error: could not create framerate estimator\n");
		ret = 1;
		goto exit;
	}
	if (mjv_source_open(s) == 0) {
		log_error("Error: could not open config source\n");
		ret = 1;
		goto exit;
	}
	// Create pipe pair to signal quit message to grabber, using the self-pipe trick:
	if (make_selfpipe_pair() == 0) {
		log_error("Error: could not create pipe\n");
		ret = 1;
		goto exit;
	}
	mjv_grabber_set_selfpipe(g, read_fd);

	// Grabbed frames will be handled by got_frame_callback():
	mjv_grabber_set_callback(g, got_frame_callback, fr);

	// Install signal handler to trap INT and TERM:
	sig_setup();

	// Run the grabber; control stays here until the stream terminates or the user interrupts:
	mjv_grabber_run(g);

	log_info("Frames processed: %d\n", n_frames);

exit:	mjv_framerate_destroy(&fr);
	if (g) {
		mjv_grabber_close_selfpipe(g);
		mjv_grabber_destroy(&g);
	}
	if (write_fd >= 0) {
		close(write_fd);
		write_fd = -1;
	}
	mjv_source_destroy(&s);
	free(pass);
	free(user);
	free(path);
	free(host);
	free(name);
	free(filename);
	return ret;
}
