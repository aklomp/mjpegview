#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>

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
