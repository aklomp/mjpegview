#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mjv_log.c"
#include "mjv_frame.h"
#include "mjv_source.h"
#include "filename.h"
#include "framerate.h"
#include "mjv_grabber.h"
#include "selfpipe.h"

// This is a really simple framegrabber for mjpeg streams. It is intended to
// compile with the least possible dependencies, to make it useful on headless,
// underpowered systems. It also keeps the rest of the modules "honest" in
// terms of simple interfaces and modularity.

static int n_frames = 0;
static int read_fd, write_fd;

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

struct cmdopts {
	char *name;
	char *filename;
	char *host;
	char *path;
	char *user;
	char *pass;
	int port;
	int usec;
};

static bool
process_cmdline (int argc, char **argv, struct cmdopts *opts)
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
			case 'm': opts->usec = atoi(optarg); break;
			case 'q': opts->port = atoi(optarg); break;
			case 'f': if (copy_string(optarg, &opts->filename)) break; return false;
			case 'H': if (copy_string(optarg, &opts->host)) break; return false;
			case 'n': if (copy_string(optarg, &opts->name)) break; return false;
			case 'u': if (copy_string(optarg, &opts->user)) break; return false;
			case 'p': if (copy_string(optarg, &opts->pass)) break; return false;
			case 'P': if (copy_string(optarg, &opts->path)) break; return false;
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
	if ((filename = filename_forge(srcname, framenum, "%f.jpg")) == NULL) {
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
	struct framerate *fr = data;

	n_frames++;
	log_debug("got frame %d\n", n_frames);

	// Feed the framerate estimator, get estimate:
	framerate_insert_datapoint(fr, mjv_frame_get_timestamp(f));
	print_info(n_frames, framerate_estimate(fr));

	// Write to file:
	write_image_file((char *)mjv_frame_get_rawbits(f), mjv_frame_get_num_rawbits(f), NULL, n_frames, mjv_frame_get_timestamp(f));

	// We are responsible for freeing the mjv_frame when we're done with it:
	mjv_frame_destroy(&f);
}

static void
sig_handler (int signum, siginfo_t *info, void *ptr)
{
	(void)signum;
	(void)info;
	(void)ptr;

	// Terminate a thread by sending a byte of data through the write end
	// of the self-pipe. The grabber catches this and exits gracefully:
	selfpipe_write_close(&write_fd);
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
	struct mjv_source *s = NULL;
	struct mjv_grabber *g = NULL;
	struct framerate *fr = NULL;

	struct cmdopts opts =
		{ .name = NULL
		, .filename = NULL
		, .host = NULL
		, .path = NULL
		, .user = NULL
		, .pass = NULL
		, .port = 0
		, .usec = 100
		} ;

	if (!process_cmdline(argc, argv, &opts)) {
		ret = 1;
		goto exit;
	}
	if (opts.filename != NULL) {
		if ((s = mjv_source_create_from_file(opts.name, opts.filename, opts.usec)) == NULL) {
			log_error("Error: could not create source from file\n");
			ret = 1;
			goto exit;
		}
	}
	else if (opts.host != NULL) {
		if ((s = mjv_source_create_from_network(opts.name, opts.host, opts.path, opts.user, opts.pass, opts.port)) == NULL) {
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
	if ((fr = framerate_create(15)) == NULL) {
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
	if (selfpipe_pair(&read_fd, &write_fd) == false) {
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

exit:	framerate_destroy(&fr);
	if (g) {
		selfpipe_read_close(&read_fd);
		mjv_grabber_destroy(&g);
	}
	if (write_fd >= 0) {
		close(write_fd);
		write_fd = -1;
	}
	mjv_source_destroy(&s);
	free(opts.pass);
	free(opts.user);
	free(opts.path);
	free(opts.host);
	free(opts.name);
	free(opts.filename);
	return ret;
}
