#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

bool
selfpipe_pair (int *restrict read_fd, int *restrict write_fd)
{
	int pfd[2];
	int flags;

	if (pipe(pfd) == -1) {
		*read_fd = *write_fd = -1;
		return false;
	}
	// Make nonblocking:
	for (int i = 0; i < 2; i++) {
		if ((flags = fcntl(pfd[i], F_GETFL)) == -1) {
			goto err;
		}
		if (fcntl(pfd[i], F_SETFL, flags | O_NONBLOCK) == -1) {
			goto err;
		}
	}
	*read_fd = pfd[0];
	*write_fd = pfd[1];
	return true;

err:	close(pfd[0]);
	close(pfd[1]);
	*read_fd = *write_fd = -1;
	return false;
}

static void
selfpipe_close (int *fd)
{
	// Close one part of a selfpipe. Can be used for both ends, but other
	// modules should call selfpipe_write_close() to write and close the
	// write end, then selfpipe_read_close() to close the read end.
	if (*fd >= 0) {
		close(*fd);
	}
	*fd = -1;
}

void
selfpipe_write_close (int *write_fd)
{
	// Terminate a thread by sending a byte of data through the write end
	// of the self-pipe. The party listening to the read socket catches
	// this and exits gracefully:
	if (*write_fd < 0) {
		return;
	}
	while (write(*write_fd, "X", 1) == -1 && errno == EAGAIN) {
		continue;
	}
	selfpipe_close(write_fd);
}

void
selfpipe_read_close (int *read_fd)
{
	selfpipe_close(read_fd);
}
