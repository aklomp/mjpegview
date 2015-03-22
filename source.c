#include <sys/select.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "mjv_log.h"
#include "source.h"

bool
source_init (
	struct source *s,
	const char *const name,
	bool (*open)(struct source *),
	void (*close)(struct source *),
	void (*destroy)(struct source **))
{
	const char *n = (name == NULL) ? "(unnamed)" : name;

	if ((s->name = strdup(n)) == NULL) {
		return false;
	}
	s->open = open;
	s->close = close;
	s->destroy = destroy;
	s->fd = -1;
	s->selfpipe_readfd = -1;
	return true;
}

void
source_deinit (struct source *s)
{
	if (s != NULL) {
		free(s->name);
	}
}

void
source_set_selfpipe (struct source *s, int pipe_read_fd)
{
	if (s != NULL) {
		s->selfpipe_readfd = pipe_read_fd;
	}
}

ssize_t
source_read (struct source *s, void *buf, size_t bufsize)
{
	int available, fdmax;
	fd_set fdset;
	struct timeval timeout;
	ssize_t nread;

	// FD_SET contains the source's file descriptor,
	// and the selfpipe file descriptor for canceling a pending read:
	FD_ZERO(&fdset);
	FD_SET(s->fd, &fdset);
	fdmax = s->fd + 1;
	if (s->selfpipe_readfd >= 0) {
		FD_SET(s->selfpipe_readfd, &fdset);
		if (s->selfpipe_readfd > s->fd) {
			fdmax = s->selfpipe_readfd + 1;
		}
	}
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	while ((available = select(fdmax, &fdset, NULL, NULL, &timeout)) == -1 && errno == EINTR) {
		continue;
	}
	// Timeout reached:
	if (available == 0) {
		log_info("Timeout reached. Giving up.\n");
		return -1;
	}
	// Other failure:
	if (available < 0) {
		log_error("Read error: %s\n", strerror(errno));
		return -1;
	}
	// Check if a byte entered through the end of the self-pipe;
	// this indicates that we need to exit the loop:
	if (s->selfpipe_readfd >= 0 && FD_ISSET(s->selfpipe_readfd, &fdset)) {
		return -1;
	}
	if ((nread = read(s->fd, buf, bufsize)) < 0) {
		log_error("Read error: %s\n", strerror(errno));
	}
	return nread;
}

const char *
source_get_name (struct source *const s)
{
	return s->name;
}
