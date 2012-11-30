#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "mjv_log.h"
#include "mjv_source.h"
#include "mjv_frame.h"
#include "mjv_grabber.h"

// Buffer must be large enough to hold the entire JPEG frame:
#define BUF_SIZE	100000

// The string length of a constant character array is one less
// than its apparent size, because of the zero terminator:
#define STR_LEN(x)	(sizeof(x) - 1)

// The number of bytes between the anchor and the cur pointer,
// inclusive:
#define LINE_LEN	(unsigned int)(s->cur + 1 - s->anchor)

// Compare the 16-bit value at a certain location with a constant:
#define U16_BYTESWAP(y)	(((((uint16_t)y) >> 8) & 0xFF) | ((((uint16_t)y) & 0xFF) << 8))
#define VALUE_AT(x,y)	(*((uint16_t *)(x)) == U16_BYTESWAP(y))

// States in our state machine:
enum states {
	STATE_HTTP_BANNER,
	STATE_HTTP_HEADER,
	STATE_FIND_BOUNDARY,
	STATE_HTTP_SUBHEADER,
	STATE_FIND_IMAGE,
	STATE_IMAGE_BY_CONTENT_LENGTH,
	STATE_IMAGE_BY_EOF_SEARCH
};

enum { READ_SUCCESS, OUT_OF_BYTES, CORRUPT_HEADER, READ_ERROR };
enum { MULTIPART_MIXED, MULTIPART_X_MIXED_REPLACE };

char header_content_type_one[] = "Content-Type:";
char header_content_type_two[] = "Content-type:";
char header_content_length_one[] = "Content-Length:";
char header_content_length_two[] = "Content-length:";

struct mjv_grabber
{
	int nread;		// return value of read();
	int mimetype;
	enum states state;	// state machine state
	char *boundary;
	int delay_usec;
	unsigned int boundary_len;
	unsigned int response_code;
	unsigned int content_length;
	struct timespec last_emitted;
	struct mjv_source *source;

	char *buf;	// read buffer;
	char *cur;	// current char under inspection in buffer;
	char *head;	// where the current read starts;
	char *anchor;	// the first byte in the buffer to keep;

	// This callback function is called whenever
	// a mjv_frame object is created by a source:
	void (*callback)(struct mjv_frame *, void *);
	void *user_pointer;
};

static int fetch_header_line (struct mjv_grabber *, char **, unsigned int *);
static inline int increment_cur (struct mjv_grabber *);
static inline bool is_numeric (char);
static inline unsigned int simple_atoi (const char *, const char *);
static int interpret_content_type (struct mjv_grabber *, char *, unsigned int);
static bool got_new_frame (struct mjv_grabber *, char *, unsigned int);
static void adjust_streambuf (struct mjv_grabber *);
static void artificial_delay (unsigned int, struct timespec *);

static int state_http_banner (struct mjv_grabber *);
static int state_http_header (struct mjv_grabber *);
static int state_find_boundary (struct mjv_grabber *);
static int state_http_subheader (struct mjv_grabber *);
static int state_find_image (struct mjv_grabber *);
static int state_image_by_content_length (struct mjv_grabber *);
static int state_image_by_eof_search (struct mjv_grabber *);

struct mjv_grabber *
mjv_grabber_create (struct mjv_source *source)
{
	struct mjv_grabber *s = NULL;

	// Allocate memory for the structure:
	if ((s = malloc(sizeof(*s))) == NULL) {
		goto err;
	}
	if ((s->buf = malloc(BUF_SIZE)) == NULL) {
		goto err;
	}
	// Set default values:
	s->boundary = NULL;
	s->mimetype = -1;
	s->content_length = 0;
	s->delay_usec = 0;
	s->state = STATE_HTTP_BANNER;
	s->last_emitted.tv_sec = 0;
	s->last_emitted.tv_nsec = 0;
	s->source = source;

	s->callback = NULL;
	s->user_pointer = NULL;

	s->anchor = NULL;
	s->cur = s->head = s->buf;

	return s;

err:	if (s != NULL) {
		free(s->buf);
		free(s);
	}
	return NULL;
}

void
mjv_grabber_destroy (struct mjv_grabber **s)
{
	if (s == NULL || *s == NULL) {
		return;
	}
	log_info("Destroying source %s\n", mjv_source_get_name((*s)->source));
	free((*s)->boundary);
	free((*s)->buf);
	free(*s);
	*s = NULL;
}

void
mjv_grabber_set_callback (struct mjv_grabber *s, void (*got_frame_callback)(struct mjv_frame*, void*), void *user_pointer)
{
	s->callback = got_frame_callback;
	s->user_pointer = user_pointer;
}

static void
adjust_streambuf (struct mjv_grabber *s)
{
	// Cheap test: if anchored at start of buffer, nothing to do:
	if (s->anchor == s->buf) {
		return;
	}
	// First byte to keep is either the byte at the anchor,
	// or the byte at cur:
	char *keepfrom = (s->anchor == NULL) ? s->cur : s->anchor;

	// How much bytes at the start to shift over:
	unsigned int offset = keepfrom - s->buf;

	// How much bytes left from keep till end?
	unsigned int good_bytes = s->head - keepfrom;

	// If important bytes left in buffer at an offset, move them:
	if (good_bytes > 0 && offset > 0) {
		memmove(s->buf, keepfrom, good_bytes);
		s->cur -= offset;
		s->head -= offset;
		s->anchor -= offset;
		return;
	}
	// Else if no anchor, reset to start of buffer:
	if (s->anchor == NULL) {
		s->cur = s->head = s->buf;
	}
}

enum mjv_grabber_status
mjv_grabber_run (struct mjv_grabber *s)
{
	int fd;
	int available;
	fd_set fdset;
	struct timespec timeout;

	// Jump table per state; order corresponds with
	// the state enum at the top of this file:
	int (*state_jump_table[])(struct mjv_grabber *) = {
		state_http_banner,
		state_http_header,
		state_find_boundary,
		state_http_subheader,
		state_find_image,
		state_image_by_content_length,
		state_image_by_eof_search
	};
	if ((fd = mjv_source_get_fd(s->source)) < 0) {
		log_error("Invalid file descriptor\n");
		return MJV_GRABBER_READ_ERROR;
	}
	for (;;)
	{
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;

		// Make this thread explicitly cancelable here:
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		// pselect() is a pthread cancellation point. When this thread
		// receives a cancellation request, it will be inside here.
		available = pselect(fd + 1, &fdset, NULL, NULL, &timeout, NULL);

		// Make this thread uncancelable; once processing IO,
		// it should be allowed to finish its job.
		// Note: this is fine when all goes well, but not when it doesn't.
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (available == 0) {
			// timeout reached
			log_info("Timeout reached. Giving up.\n");
			return MJV_GRABBER_TIMEOUT;
		}
		if (available < 0) {
			if (errno == EINTR) {
				continue;
			}
			log_error("%s\n", strerror(errno));
			return MJV_GRABBER_READ_ERROR;
		}
		s->nread = read(fd, s->head, BUF_SIZE - (s->head - s->buf));
		if (s->nread < 0) {
			log_error("%s\n", strerror(errno));
			return MJV_GRABBER_READ_ERROR;
		}
		else if (s->nread == 0) {
			log_info("End of file\n");
			return MJV_GRABBER_PREMATURE_EOF;
		}
		// buflast is always ONE PAST the real last char:
		s->head += s->nread;

		log_debug("Read %u bytes\n", s->nread);

		// Dispatcher; while successful, keep jumping from state to state:
next_state: 	switch (state_jump_table[s->state](s))
		{
			case READ_SUCCESS:
				goto next_state;

			case OUT_OF_BYTES:
				adjust_streambuf(s);
				continue;

			case READ_ERROR:
				log_error("READ_ERROR\n");
				return MJV_GRABBER_READ_ERROR;

			case CORRUPT_HEADER:
				log_error("Corrupt header\n");
				return MJV_GRABBER_CORRUPT_HEADER;
		}
	}
	return MJV_GRABBER_SUCCESS;
}

static int
state_http_banner (struct mjv_grabber *s)
{
	int ret;
	char *line;
	unsigned int line_len;
	char http_signature[] = "HTTP/1.";

	// Keep looping inside this function till it finally returns
	// with success, or it reports back a read failure to the main loop:
	if ((ret = fetch_header_line(s, &line, &line_len)) != READ_SUCCESS) {
		return ret;
	}
	// We expect the very first thing in the response to be the HTTP signature:
	if (line_len < STR_LEN(http_signature)
	 || strncmp(line, "HTTP/1.", STR_LEN(http_signature)) != 0) {
		log_error("Corrupt HTTP signature\n");
		return CORRUPT_HEADER;
	}
	// HTML version, accept 1.0 and 1.1:
	if (line_len < 8
	 || (line[7] != '0' && line[7] != '1')) {
		log_error("Corrupt HTTP signature\n");
		return CORRUPT_HEADER;
	}
	// Status code:
	if (line_len < 12
	 || line[8] != ' '
	 || !is_numeric(line[9])
	 || !is_numeric(line[10])
	 || !is_numeric(line[11])) {
		log_error("Corrupt HTTP status code\n");
		return CORRUPT_HEADER;
	}
	// HTML response code in bytes 9..11:
	s->response_code = simple_atoi(&line[9], &line[11]);

	if (s->response_code != 200) {
		// TODO: something better than this:
		log_error("Response code is not 200 but %u\n", s->response_code);
		return READ_ERROR;
	}
	s->state = STATE_HTTP_HEADER;
	return increment_cur(s);
}

static int
state_http_header (struct mjv_grabber *s)
{
	int ret;
	char *line;
	unsigned int line_len;

	for (;;)
	{
		if ((ret = fetch_header_line(s, &line, &line_len)) != READ_SUCCESS) {
			return ret;
		}
		// An empty header line signifies the end of the header:
		if (line_len == 0) {
			// If we did not manage to find the boundary, quit with error:
			// TODO: implement start-of-image/end-of-image search instead
			// as a last-ditch method
			if (s->boundary == NULL) {
				log_error("Could not find boundary\n");
				return CORRUPT_HEADER;
			}
			s->state = STATE_FIND_BOUNDARY;
			break;
		}
#define STRING_MATCH(x)	(line_len >= STR_LEN(x) && strncmp(line, x, STR_LEN(x)) == 0)
		if (STRING_MATCH(header_content_type_one)
		 || STRING_MATCH(header_content_type_two)) {
			if ((ret = interpret_content_type(s, line, line_len)) != READ_SUCCESS) {
				return ret;
			}
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
#undef STRING_MATCH
	}
	return increment_cur(s);
}

static int
state_find_boundary (struct mjv_grabber *s)
{
	// Loop over the input till we find a \r\n or an \n followed by
	// our boundary marker, and another \r\n or \n:
	for (;;)
	{
		// If this character matches the first character of the boundary,
		// then it might be the start of a match; set the anchor:
		if (s->anchor == NULL) {
			if (*s->cur == *s->boundary) {
				s->anchor = s->cur;
			}
		}
		else {
			// Complete boundary not yet found, and we have a non-matching character;
			// restart the scan:
			if (LINE_LEN <= s->boundary_len && *s->cur != s->boundary[s->cur - s->anchor]) {
				s->cur = s->anchor + 1;
				s->anchor = NULL;
			}
			// If successfully found boundary plus one byte, and that byte is \n, then success:
			if (LINE_LEN == s->boundary_len + 1 && *s->cur == (char)0x0a) {
				s->anchor = NULL;
				s->state = STATE_HTTP_SUBHEADER;
				break;
			}
			// If successfully found boundary plus two bytes...
			if (LINE_LEN == s->boundary_len + 2) {
				// ..and those two bytes are \r\n, then success...
				if (VALUE_AT(s->cur - 1, 0x0d0a)) {
					s->anchor = NULL;
					s->content_length = 0;
					s->state = STATE_HTTP_SUBHEADER;
					break;
				}
				// ...else back to square one:
				s->cur = s->anchor + 1;
				s->anchor = NULL;
			}
		}
		// Move on to next byte:
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
	return increment_cur(s);
}

static int
state_http_subheader (struct mjv_grabber *s)
{
	int ret;
	char *line;
	unsigned int line_len;

	for (;;)
	{
		if ((ret = fetch_header_line(s, &line, &line_len)) != READ_SUCCESS) {
			return ret;
		}
		if (line_len == 0) {
			break;
		}

#define STRING_MATCH(x)	(line_len >= STR_LEN(x) && strncmp(line, x, STR_LEN(x)) == 0)

		if (STRING_MATCH(header_content_length_one)
		 || STRING_MATCH(header_content_length_two)) {
			char *cur = line + STR_LEN(header_content_length_one);
			char *last = line + line_len - 1;
			char *num_start;
			char *num_end;
			while (*cur == ' ') {
				if (cur++ == last) {
					return CORRUPT_HEADER;
				}
			}
			num_start = cur;
			while (is_numeric(*cur)) {
				cur++;
			}
			num_end = cur - 1;
			if (num_end > num_start) {
				s->content_length = simple_atoi(num_start, num_end);
			}
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
	s->state = STATE_FIND_IMAGE;
	return increment_cur(s);

#undef STRING_MATCH
}

static int
state_find_image (struct mjv_grabber *s)
{
	// Consume bytes from the buffer till we find the
	// JPEG signature, which is 0xffd8:
	for (;;)
	{
		if (*s->cur == (char)0xff) {
			s->anchor = s->cur;
		}
		else if ((s->cur - s->anchor) == 1 && *s->cur == (char)0xd8) {
			// If the content length is known, we can use it to
			// take a shortcut; else brute search for the EOF:
			s->state = (s->content_length > 0)
				? STATE_IMAGE_BY_CONTENT_LENGTH
				: STATE_IMAGE_BY_EOF_SEARCH;

			// Check that the whole of the image can fit in the buffer.
			// FIXME: do something nicer:
			if (s->content_length > BUF_SIZE) {
				log_error("Content length larger than read buffer; frame won't fit\n");
				log_error("Skipping frame, sorry.\n");
				s->state = STATE_FIND_BOUNDARY;
			}
			break;
		}
		else {
			s->anchor = NULL;
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
	return increment_cur(s);
}

static int
state_image_by_content_length (struct mjv_grabber *s)
{
#define BYTES_LEFT_IN_BUF (s->head - s->cur - 1)
#define BYTES_FOUND  (s->cur + 1 - s->anchor)
#define BYTES_NEEDED (intptr_t)(s->content_length - BYTES_FOUND)

	// If we have a content-length > 0, then trust it; read out
	// exactly that many bytes before finding the boundary again.
	for (;;)
	{
		// If more than enough bytes left at end of buffer,
		// we have our image:
		if (BYTES_LEFT_IN_BUF >= BYTES_NEEDED)
		{
			// Skip pointer ahead to last byte of image:
			s->cur = s->anchor + s->content_length - 1;

			// Report the new frame:
			got_new_frame(s, s->anchor, s->content_length);

			// Release anchor, reset content length:
			s->anchor = NULL;
			s->content_length = 0;

			// Move to next state, get back to work:
			s->state = STATE_FIND_BOUNDARY;
			break;
		}
		// Else slice off as much as we can and return for more:
		s->cur = s->head;
		return OUT_OF_BYTES;
	}
	return increment_cur(s);

#undef BYTES_NEEDED
#undef BYTES_FOUND
#undef BYTES_LEFT_IN_BUF
}

static int
state_image_by_eof_search (struct mjv_grabber *s)
{
	// If no content-length known, then there's nothing we can
	// do but seek the EOF marker, 0xffd9, one byte at a time:
	for (;;)
	{
		// If we found the EOF marker, export the frame and be done:
		if (VALUE_AT(s->cur - 1, 0xffd9)) {
			got_new_frame(s, s->anchor, s->cur - s->anchor + 1);
			s->anchor = NULL;
			s->state = STATE_FIND_BOUNDARY;
			return increment_cur(s);
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
}

static inline bool
is_numeric (char c)
{
	return (c >= '0' && c <= '9');
}

static inline int
increment_cur (struct mjv_grabber *s)
{
	s->cur++;
	return (s->cur >= s->head) ? OUT_OF_BYTES : READ_SUCCESS;
}

static int
fetch_header_line (struct mjv_grabber *s, char **line, unsigned int *line_len)
{
	// Assume s->cur is on the first character of the line
	// Consume buffer until we hit a line terminator:
	if (s->anchor == NULL) {
		s->anchor = s->cur;
	}
	for (;;)
	{
		// Search for \n's; some cameras do not use the \r\n convention,
		// but plain \n as a line terminator:
		if (*s->cur == (char)0x0a) {
			// If preceded by an 0x0d, count that as a line terminator too:
			if (LINE_LEN >= 2 && *(s->cur - 1) == (char)0x0d) {
				*line_len = LINE_LEN - 2;
			}
			else {
				*line_len = LINE_LEN - 1;
			}
			*line = s->anchor;
			s->anchor = NULL;
			return READ_SUCCESS;
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
}

static inline unsigned int
simple_atoi (const char *first, const char *last)
{
	// Really simple. Only does smallish positive integers,
	// under the assumption that all chars are '0'..'9'.
	const char *c = first;
	unsigned int i = 0;
	while (c <= last) {
		i = i * 10 + (*c++ - '0');
	}
	return i;
}

static int
interpret_content_type (struct mjv_grabber *s, char *line, unsigned int line_len)
{
	char *cur;
	char *last;
	char multipart_x_mixed_replace[] = "multipart/x-mixed-replace";
	char multipart_mixed[] = "multipart/mixed";
	char boundary[] = "boundary=";

	cur = line + STR_LEN(header_content_type_one);
	last = line + line_len - 1;

#define SIZE_LEFT	(unsigned int)(last + 1 - cur)
#define SKIP_SPACES	while (*cur == ' ') { if (cur++ == last) return CORRUPT_HEADER; }
#define STRING_MATCH(x)	(SIZE_LEFT >= (intptr_t)STR_LEN(x) && strncmp(cur, x, STR_LEN(x)) == 0)

	SKIP_SPACES;

	// Try to establish mime type:
	if (STRING_MATCH(multipart_x_mixed_replace)) {
		s->mimetype = MULTIPART_X_MIXED_REPLACE;
		cur += STR_LEN(multipart_x_mixed_replace);
	}
	else if (STRING_MATCH(multipart_mixed)) {
		s->mimetype = MULTIPART_MIXED;
		cur += STR_LEN(multipart_mixed);
	}
	// The rest of this field is divided into subfields by semicolons.
	// So split the field on the semicolons:
	for (;;)
	{
		// Find next semicolon or EOL:
		while (*cur != ';') {
			if (cur++ == last) {
				return READ_SUCCESS;
			}
		}
		// Advance past semicolon:
		if (cur++ == last) {
			return READ_SUCCESS;
		}
		// Skip spaces:
		while (*cur == ' ') {
			if (cur++ == last) {
				return READ_SUCCESS;
			}
		}
		// Try to match interesting subfields.
		// For now, the boundary:
		if (STRING_MATCH(boundary)) {
			char *end;
			cur += STR_LEN(boundary);
			end = cur;
			// Everything up to EOL or next semicolon is boundary:
			while (*end != ';' && end != last) {
				end++;
			}
			if (end > cur) {
				// Allocate storage for boundary, copy over:
				s->boundary_len = end - cur + 1;
				if ((s->boundary = malloc(s->boundary_len + 1)) == NULL) {
					// TODO: better handling
					log_error("malloc()");
					return READ_ERROR;
				}
				memcpy(s->boundary, cur, s->boundary_len);
				s->boundary[s->boundary_len] = 0;
			}
		}
	}
	return READ_SUCCESS;

#undef SIZE_LEFT
#undef SKIP_SPACES
#undef STRING_MATCH
}

static bool
got_new_frame (struct mjv_grabber *s, char *start, unsigned int len)
{
	struct mjv_frame *frame;

#if 0
	// Quick validity check on the frame;
	// must start with 0xffd8 and end with 0xffd9:
	if (!VALUE_AT(start, 0xffd8)) {
		log_error("Invalid start marker!\n");
	}
	if (!VALUE_AT(start + len - 2, 0xffd9)) {
		log_error("%s: invalid JPEG EOF signature\n", s->name);
		{
			char *c;
			for (c = start + len - 20; c < start + len + 20; c++) {
				if (VALUE_AT(c, 0xffd9)) log_error("Off by %i??\n", c - (start + len - 2));
			}
		}
	}
#endif
	if (s->delay_usec > 0) {
		artificial_delay(s->delay_usec, &s->last_emitted);
	}
	if (s->callback == NULL) {
		log_error("No callback defined for frame\n");
		return false;
	}
	if ((frame = mjv_frame_create(start, len)) == NULL) {
		log_error("Could not create frame\n");
		return false;
	}
	s->callback(frame, s->user_pointer);

	return true;
}

static void
artificial_delay (unsigned int delay_usec, struct timespec *last)
{
	struct timespec now;
	unsigned long delay_sec;
	unsigned long delay_nsec;

	if (delay_usec < 1000000) {
		delay_sec = 0;
		delay_nsec = delay_usec * 1000;
	}
	else {
		delay_sec = delay_usec / 1000000;
		delay_nsec = (delay_usec % 1000000) * 1000;
	}
	clock_gettime(CLOCK_REALTIME, &now);

	// Need a nonzero previous timestamp to time against:
	if (last->tv_sec > 0)
	{
		// Calculate scheduled awakening:
		now.tv_sec  = last->tv_sec  + delay_sec;
		now.tv_nsec = last->tv_nsec + delay_nsec;
		if (now.tv_nsec >= 1000000000) {
			now.tv_sec++;
			now.tv_nsec -= 1000000000;
		}
		// Keep sleeping across interrupts until the now has come to pass:
		while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &now, NULL) != 0);
	}
	// Update the last emission time to the now:
	memcpy(last, &now, sizeof(struct timespec));
}
