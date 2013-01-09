#include <stdlib.h>
#include <string.h>

static size_t
framenum_expanded_len (unsigned int framenum)
{
	if (framenum >= 1000000000) return 10;
	if (framenum >= 100000000) return 9;
	if (framenum >= 10000000) return 8;
	if (framenum >= 1000000) return 7;
	if (framenum >= 100000) return 6;
	if (framenum >= 10000) return 5;
	if (framenum >= 1000) return 4;
	if (framenum >= 100) return 3;
	if (framenum >= 10) return 2;
	return 1;
}

static void
itoa (unsigned int n, char *buf, size_t known_length)
{
	unsigned int q;

	// Start at least significant (rightmost) digit,
	// progress to most significant (leftmost) digit:
	for (char *p = buf + known_length - 1; p >= buf; p--) {
		q = n / 10;
		*p = '0' + (n - q * 10);
		n = q;
	}
	buf[known_length] = '\0';
}

char *
mjv_filename_forge (const char *const srcname, unsigned int framenum, char *const pat)
{
	char *c, *p, *buf;
	char *name = NULL;
	char fnum[11];
	size_t buflen = strlen(pat);	// Account for the \0 terminator later
	unsigned int n_n = 0;
	unsigned int n_f = 0;

	// %n : source name
	// %f : frame number

	// Count number of %n's and %f's in pattern:
	for (char *c = pat; *c; c++) {
		if (*c == '%') {
		       if (*(c + 1) == 'f') n_f++;
		       if (*(c + 1) == 'n') n_n++;
		}
	}
	// Find expansion size of frame number:
	if (n_f > 0) {
		size_t framenum_len = framenum_expanded_len(framenum);
		buflen += ((int)framenum_len - 2) * n_f;
		itoa(framenum, fnum, framenum_len);
	}
	// Find expansion size of source name:
	if (n_n > 0) {
		size_t srcname_len = strlen(name = (char *)srcname);
		buflen += (srcname_len - 2) * n_n;
	}
	// Allocate buffer:
	if ((buf = malloc(buflen + 1)) == NULL) {
		return NULL;
	}
	// Copy pattern to buffer, filling in the placeholders:
	for (c = pat, p = buf; *c; c++) {
		if (*c != '%') {
			*p++ = *c;
			continue;
		}
		if (n_f && *(c + 1) == 'f') {
			char *f = fnum;
			while (*f) {
				*p++ = *f++;
			}
			c++;
			continue;
		}
		if (n_n && *(c + 1) == 'n') {
			char *n = name;
			while (*n) {
				*p++ = *n++;
			}
			c++;
			continue;
		}
		*p++ = *c;
	}
	*p = '\0';
	return buf;
}
