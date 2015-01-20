#include "../selfpipe.c"

static int
test_createdestroy ()
{
	int readfd, writefd;

	if (selfpipe_pair(&readfd, &writefd) == false) {
		return 1;
	}
	selfpipe_close(&readfd);
	selfpipe_close(&writefd);
	return 0;
}

static int
test_writepipe ()
{
	int readfd, writefd;
	char buf;

	if (selfpipe_pair(&readfd, &writefd) == false) {
		return 1;
	}
	selfpipe_write_close(&writefd);

	// There should be a byte of input at readfd now:
	if (read(readfd, &buf, 1) < 0) {
		selfpipe_close(&readfd);
		return 1;
	}
	// The byte of input should be 'X':
	if (buf != 'X') {
		selfpipe_close(&readfd);
		return 1;
	}
	selfpipe_close(&readfd);
	return 0;
}

int
main ()
{
	int ret = 0;

	ret |= test_createdestroy();
	ret |= test_writepipe();

	return ret;
}
