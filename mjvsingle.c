#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mjv_log.c"
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_grabber.h"

// This is a really simple framegrabber for mjpeg streams. It is intended to
// compile with the least possible dependencies, to make it useful on headless,
// underpowered systems. It also keeps the rest of the modules "honest" in
// terms of simple interfaces and modularity.

static int n_frames = 0;

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
write_image_file (char *data, size_t nbytes, unsigned int framenum, const struct timespec *const timestamp)
{
	FILE *fp;
	char filename_pat[] = "%0_d.jpg";
	size_t filename_len;
	char patsize;

	if (data == NULL || nbytes == 0) {
		log_error("Error: frame contains no data\n");
		return;
	}
	if (framenum >= 1000000000) {
		log_error("Error: framenum too large (over 9 digits)\n");
		return;
	}
	patsize = (framenum >= 100000000) ? 9
		: (framenum >= 10000000) ? 8
		: (framenum >= 1000000) ? 7
		: (framenum >= 100000) ? 6
		: (framenum >= 10000) ? 5
		: (framenum >= 1000) ? 4
		: 3;

	filename_pat[2] = '0' + patsize;
	filename_len = sizeof(filename_pat) + patsize - 4;
	{
		char filename[filename_len];

		snprintf(filename, filename_len, filename_pat, framenum);
		if ((fp = fopen(filename, "w")) == NULL) {
			perror("fopen()");
		}
		else {
			log_debug("writing %s\n", filename);
			fwrite(data, nbytes, 1, fp);
			fclose(fp);
			timestamp_file(filename, timestamp);
		}
	}
}

static void
got_frame_callback (struct mjv_frame *f, void *data)
{
	(void)data;

	n_frames++;
	log_debug("got frame %d\n", n_frames);

	// Write to file:
	write_image_file((char *)mjv_frame_get_rawbits(f), mjv_frame_get_num_rawbits(f), n_frames, mjv_frame_get_timestamp(f));

	// We are responsible for freeing the mjv_frame when we're done with it:
	mjv_frame_destroy(f);
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
	if (mjv_source_open(s) == 0) {
		log_error("Error: could not open config source\n");
		ret = 1;
		goto exit;
	}
	// Grabbed frames will be handled by got_frame_callback():
	mjv_grabber_set_callback(g, got_frame_callback, NULL);

	// Run the grabber; control stays here until the stream terminates or the user interrupts:
	mjv_grabber_run(g);

	log_info("Frames processed: %d\n", n_frames);

exit:	mjv_grabber_destroy(g);
	mjv_source_destroy(s);
	free(pass);
	free(user);
	free(path);
	free(host);
	free(name);
	free(filename);
	return ret;
}
