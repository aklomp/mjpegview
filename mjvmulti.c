#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "mjv_config.h"
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

struct thread {
	struct mjv_source *s;
	struct mjv_grabber *g;
	struct framerate *fr;
	int n_frames;
	int read_fd;
	int write_fd;

	pthread_t pthread;
	struct thread *next;
};

static int quit_flag = 0;

static void
process_cmdline (int argc, char **argv, char **filename)
{
	int c, option_index = 0;
	static struct option long_options[] = {
		{ "debug", 0, 0, 'd' },
		{ "filename", 1, 0, 'f' },
		{ "help", 0, 0, 'h' },
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
			case 'f': *filename = strdup(optarg); break;
		}
	}
}

static void
thread_destroy (struct thread **t)
{
	if (t == NULL || *t == NULL) {
		return;
	}
	if ((*t)->read_fd >= 0) {
		close((*t)->read_fd);
		mjv_grabber_set_selfpipe((*t)->g, -1);
	}
	if ((*t)->write_fd >= 0) {
		close((*t)->write_fd);
	}
	framerate_destroy(&(*t)->fr);
	mjv_grabber_destroy(&(*t)->g);
	free(*t);
	*t = NULL;
}

static void
thread_cancel (struct thread *t)
{
	// Terminate a thread by sending a byte of data through the write end
	// of the self-pipe. The grabber catches this and exits gracefully:
	if (t->write_fd < 0) {
		return;
	}
	while (write(t->write_fd, "X", 1) == -1 && errno == EAGAIN) {
		continue;
	}
	close(t->write_fd);
	t->write_fd = -1;
	pthread_join(t->pthread, NULL);
}

static struct thread *
thread_create (struct mjv_source *s)
{
	struct thread *t;

	if ((t = malloc(sizeof(*t))) == NULL) {
		return NULL;
	}
	t->g = NULL;
	t->fr = NULL;
	t->next = NULL;
	t->n_frames = 0;
	t->s = s;

	if (selfpipe_pair(&t->read_fd, &t->write_fd) == false) {
		goto err;
	}
	if ((t->g = mjv_grabber_create(t->s)) == NULL) {
		goto err;
	}
	mjv_grabber_set_selfpipe(t->g, t->read_fd);

	if ((t->fr = framerate_create(15)) == NULL) {
		goto err;
	}
	return t;

err:	thread_destroy(&t);
	return NULL;
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
write_image_file (char *data, size_t nbytes, const char *const srcname, unsigned int framenum, const struct timespec *const timestamp)
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
	if ((filename = filename_forge(srcname, framenum, "%n_%f.jpg")) == NULL) {
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
	struct thread *t = data;

	t->n_frames++;

	// Feed the framerate estimator, get estimate:
	framerate_insert_datapoint(t->fr, mjv_frame_get_timestamp(f));

	// Write to file:
	write_image_file((char *)mjv_frame_get_rawbits(f), mjv_frame_get_num_rawbits(f), mjv_source_get_name(t->s), t->n_frames, mjv_frame_get_timestamp(f));

	// We are responsible for freeing the mjv_frame when we're done with it:
	mjv_frame_destroy(&f);
}

static void *
thread_main (void *data)
{
	struct mjv_source *s = ((struct thread *)data)->s;
	struct mjv_grabber *g = ((struct thread *)data)->g;

	if (mjv_source_open(s) == 0) {
		log_error("Error: could not open config source\n");
		goto exit;
	}
	// Grabbed frames will be handled by got_frame_callback():
	mjv_grabber_set_callback(g, got_frame_callback, data);

	// Run the grabber; control stays here until the stream or the thread terminates:
	mjv_grabber_run(g);

exit:	return NULL;
}

static void
sig_handler (int signum, siginfo_t *info, void *ptr)
{
	(void)signum;
	(void)info;
	(void)ptr;

	quit_flag = 1;
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
	struct mjv_config *config = NULL;
	struct mjv_source *source = NULL;
	pthread_attr_t pthread_attr;
	struct thread *first = NULL;
	struct thread *t = NULL;
	struct thread *c = NULL;

	process_cmdline(argc, argv, &filename);

	if (filename == NULL) {
		log_error("Error: no config file specified\n");
		ret = 1;
		goto exit;
	}
	if ((config = mjv_config_create_from_file(filename)) == NULL) {
		ret = 1;
		goto exit;
	}
	// For each source, allocate a helper structure:
	for (source = mjv_config_source_first(config); source; source = mjv_config_source_next(config)) {
		if ((t = thread_create(source)) == NULL) {
			// TODO: error!
			break;
		}
		if (first == NULL) {
			first = t;
		}
		else {
			c->next = t;
		}
		c = t;
	}
	pthread_attr_init(&pthread_attr);

	// For each helper structure, kick off a thread:
	for (t = first; t; t = t->next) {
		if (pthread_create(&t->pthread, &pthread_attr, thread_main, t) != 0) {
			// TODO: error!
			break;
		}
	}
	// Wait for threads to terminate, or for user to interrupt:
	sig_setup();
	while (!quit_flag) {
		sleep(60);
	}
	// Ask the threads to terminate:
	for (t = first; t; t = t->next) {
		thread_cancel(t);
	}

	pthread_attr_destroy(&pthread_attr);
exit:	for (t = first; t; t = c) {
		c = t->next;
		thread_destroy(&t);
	}
	if (config) {
		mjv_config_destroy(&config);
	}
	free(filename);
	return ret;
}
