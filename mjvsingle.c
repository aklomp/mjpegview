#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include "mjv_log.c"
#include "mjv_frame.h"
#include "mjv_config_source.h"
#include "mjv_source.h"

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
		if ((c = getopt_long(argc, argv, "f:hH:n:m:u:p:P:q:", long_options, &option_index)) == -1) {
			break;
		}
		switch (c)
		{
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
	// If no camera name given, set a default:
	if (*name == NULL && !copy_string("Camera", name)) {
		return false;
	}
	return true;
}

static void
got_frame_callback (struct mjv_frame *f, void *data)
{
	(void)data;

	n_frames++;
	log_debug("got frame %d\n", n_frames);

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
	struct mjv_config_source *cs = NULL;
	struct mjv_source *s = NULL;

	if (!process_cmdline(argc, argv, &name, &filename, &usec, &host, &path, &user, &pass, &port)) {
		ret = 1;
		goto exit;
	}
	if (filename != NULL) {
		if ((cs = mjv_config_source_create_from_file (name, filename, usec)) == NULL) {
			log_error("Error: could not create source from file\n");
			ret = 1;
			goto exit;
		}
	}
	else if (host != NULL) {
		if ((cs = mjv_config_source_create_from_network (name, host, path, user, pass, port)) == NULL) {
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
	if ((s = mjv_source_create(cs)) == NULL) {
		log_error("Error: could not create source\n");	// TODO: non-descriptive error messages...
		ret = 1;
		goto exit;
	}
	if (mjv_config_source_open(cs) == 0) {
		log_error("Error: could not open config source\n");
		ret = 1;
		goto exit;
	}
	// Grabbed frames will be handled by got_frame_callback():
	mjv_source_set_callback(s, got_frame_callback, NULL);

	// Run the grabber; control stays here until the stream terminates or the user interrupts:
	mjv_source_capture(s);

	log_info("Frames processed: %d\n", n_frames);

exit:	mjv_source_destroy(s);
	mjv_config_source_destroy(cs);
	free(pass);
	free(user);
	free(path);
	free(host);
	free(name);
	free(filename);
	return ret;
}
