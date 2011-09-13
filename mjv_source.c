#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>	// close()
#include <fcntl.h>
#include <stdlib.h>	// malloc(), free()
#include <string.h>	// strncmp(), memcpy()
#include <glib.h>
#include <stdio.h>	// snprintf()
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>	// hints
#include <sys/select.h>
#include <arpa/inet.h>
#include <assert.h>

#include "mjv_frame.h"
#include "mjv_framebuf.h"
#include "mjv_source.h"

// Buffer must be large enough to hold the entire JPEG frame:
#define BUF_SIZE	100000
#define STR_LEN(x)	(sizeof(x) - 1)

// Debug print functions:
#if 0
  #define Debug(x...)	g_printerr(x)
  #define DebugEntry()	g_printerr("Entering %s\n", __func__)
#else
  #define Debug(x...)	((void)(0))
  #define DebugEntry()	((void)(0))
#endif

// States in our state machine:
enum {
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

static unsigned int last_id = 0;

struct mjv_source {
	int fd;
	char *name;		// pretty name for this camera
	char *buf;		// read buffer;
	char *cur;		// current char under inspection in buffer;
	char *head;		// where the current read starts;
	char *buflast;		// first char in buffer past the end;
	char *anchor;		// marks the start of a token;
	int nread;		// return value of read();
	int mimetype;
	int state;		// state machine state
	char *boundary;
	unsigned int id;
	unsigned int boundary_len;
	unsigned int response_code;
	unsigned int content_length;
	unsigned int byte_counter;
	struct mjv_framebuf *framebuf;

	// This callback function is called whenever
	// a mjv_frame object is created by a source:
	void (*got_frame_callback)(struct mjv_source *, struct mjv_frame *);
};

static struct mjv_source *mjv_source_create ();
static void reset_frame_variables (struct mjv_source *);
static int fetch_header_line (struct mjv_source*, char**, unsigned int *);
static inline int increment_cur (struct mjv_source *);
static inline bool is_numeric (char);
static inline unsigned int simple_atoi (const char *, const char *);
static int interpret_content_type (struct mjv_source *, char *, unsigned int);
static bool got_new_frame (struct mjv_source *, char *, unsigned int);

static int state_http_banner (struct mjv_source *);
static int state_http_header (struct mjv_source *);
static int state_find_boundary (struct mjv_source *);
static int state_http_subheader (struct mjv_source *);
static int state_find_image (struct mjv_source *);
static int state_image_by_content_length (struct mjv_source *);
static int state_image_by_eof_search (struct mjv_source *);

static bool
write_http_request (const int fd, const char *path, const char *username, const char *password)
{
	char *auth_string = NULL;
	gchar *base64_auth_string = NULL;
	unsigned int username_len = strlen(username);
	unsigned int password_len = strlen(password);
	char crlf[] = "\r\n";
	char get_request[] = "GET ";
	char http_signature[] = " HTTP/1.0";
	char auth_basic_header[] = "Authorization: Basic ";
	char keep_alive[] = "Connection: Keep-Alive";

#define SAFE_WRITE(x, y) \
		do { \
			if (write(fd, x, y) != (ssize_t)y) { \
				Debug("Write failed\n"); \
				goto err; \
			} \
		} while (0)

#define SAFE_WRITE_STR(x) \
		SAFE_WRITE(x, STR_LEN(x))

	DebugEntry();

	// Make HTTP request:
	SAFE_WRITE_STR(get_request);
	SAFE_WRITE(path, strlen(path));
	SAFE_WRITE_STR(http_signature);
	SAFE_WRITE_STR(crlf);
	SAFE_WRITE_STR(keep_alive);
	SAFE_WRITE_STR(crlf);

	// Add basic authentication header if credentials passed:
	if (username_len > 0 || password_len > 0)
	{
		// The auth string is 'username:password':
		if ((auth_string = malloc(username_len + password_len + 1)) == NULL) {
			goto err;
		}
		memcpy(auth_string, username, username_len);
		auth_string[username_len] = ':';
		memcpy(auth_string + username_len + 1, password, password_len);
		base64_auth_string = g_base64_encode((guchar *)auth_string, (gsize)(username_len + password_len + 1));

		SAFE_WRITE_STR(auth_basic_header);
		SAFE_WRITE(base64_auth_string, strlen(base64_auth_string));
		SAFE_WRITE_STR(crlf);

		g_free(base64_auth_string);
		free(auth_string);
		base64_auth_string = auth_string = NULL;
	}
	// Terminate HTTP header:
	SAFE_WRITE_STR(crlf);

	// Return successfully:
	return true;

#undef SAFE_WRITE_STR
#undef SAFE_WRITE

err:	if (base64_auth_string != NULL) {
		g_free(base64_auth_string);
	}
	if (auth_string != NULL) {
		free(auth_string);
	}
	return false;
}

struct mjv_source *
mjv_source_create_from_file (const char *filename, void (*got_frame_callback)(struct mjv_source*, struct mjv_frame*))
{
	struct mjv_source *s;

	if ((s = mjv_source_create(got_frame_callback)) == NULL) {
		return NULL;
	}
	if ((s->fd = open(filename, O_RDONLY)) < 0) {
		g_printerr("%s: %s\n", g_strerror(errno), filename);
		mjv_source_destroy(s);
		return NULL;
	}
	return s;
}

struct mjv_source *
mjv_source_create_from_network (const char *host, const unsigned int port, const char *path, const char *username, const char *password, void (*got_frame_callback)(struct mjv_source*, struct mjv_frame*))
{
	int fd = -1;
	char port_string[6];
	struct mjv_source *s;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	// FIXME: all char* arguments are assumed to be null-terminated,
	// but all of them come from user input! Don't be so trusting.

	if ((s = mjv_source_create(got_frame_callback)) == NULL) {
		return NULL;
	}
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_TCP;

	// Second argument of getaddrinfo() is the port number,
	// as a string; so snprintf() it:
	if (port > 65535) {
		g_printerr("Implausible port\n");
		goto err;
	}
	snprintf(port_string, sizeof(port_string), "%u", port);

	if (getaddrinfo(host, port_string, &hints, &result) != 0) {
		g_printerr("%s\n", g_strerror(errno));
		goto err;
	}
	// Loop over all results, try them until we find
	// a descriptor that works:
	for (rp = result; rp; rp = rp->ai_next) {
		if ((fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) {
			continue;
		}
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) >= 0) {
			break;
		}
		close(fd);
	}
	// Free result data:
	freeaddrinfo(result);

	// Quit if none of the results worked:
	if (rp == NULL) {
		Debug("Could not connect\n");
		goto err;
	}
	Debug("Connected\n");

	// Write the HTTP request:
	if (!write_http_request(fd, path, username, password)) {
		Debug("Could not write HTTP request\n");
		goto err;
	}
	Debug("Wrote http request\n");

	// The response is handled by the stream decoder. We're done:
	s->fd = fd;
	return s;

err:	if (fd >= 0) {
		close(fd);
	}
	mjv_source_destroy(s);
	return NULL;
}

// This function is protected; only used in this file:
static struct mjv_source *
mjv_source_create (void (*got_frame_callback)(struct mjv_source*, struct mjv_frame*))
{
	int ret;
	struct mjv_source *s = NULL;

	// Allocate memory for the structure:
	if ((s = malloc(sizeof(*s))) == NULL) {
		goto err;
	}
	// Allocate memory for the read buffer:
	if ((s->buf = malloc(BUF_SIZE)) == NULL) {
		goto err;
	}
	// Set default values:
	s->fd = -1;
	s->id = ++last_id;	// First created camera has id #1
	s->anchor = NULL;
	s->boundary = NULL;
	s->framebuf = NULL;
	s->mimetype = -1;
	s->state = STATE_HTTP_BANNER;
	s->head = s->buflast = s->cur = s->buf;
	s->got_frame_callback = got_frame_callback;
	reset_frame_variables(s);

	// Set a default name based on the ID:
	if ((s->name = malloc(15)) == NULL) {
		goto err;
	}
	ret = snprintf(s->name, 15, "Camera %u", s->id);
	if (ret < 0) {
		g_printerr("Could not snprintf() the default camera name\n");
		goto err;
	}
	if (ret >= 15) {
		g_printerr("Not enough space to snprintf() the default camera name\n");
		goto err;
	}
	// Return the new object:
	return s;

err:	if (s != NULL) {
		free(s->name);
		free(s->buf);
		free(s);
	}
	return NULL;
}

void
mjv_source_destroy (struct mjv_source *s)
{
	assert(s != NULL);
	if (s->fd >= 0 && close(s->fd) < 0) {
		Debug("%s\n", g_strerror(errno));
	}
	free(s->name);
	free(s->boundary);
	if (s->framebuf != NULL) {
		mjv_framebuf_destroy(s->framebuf);
	}
	free(s->buf);
	free(s);
}

unsigned int
mjv_source_set_name (struct mjv_source *const s, const char *const name)
{
	size_t name_len;

	assert(s != NULL);
	assert(name != NULL);

	// Destroy any existing name:
	free(s->name);

	// Check string length for ridiculousness:
	if ((name_len = strlen(name)) > 100) {
		return 0;
	}
	// Attempt to allocate memory:
	if ((s->name = malloc(name_len + 1)) == NULL) {
		return 0;
	}
	// Copy name plus terminator into memory, return success:
	memcpy(s->name, name, name_len + 1);
	return 1;
}

const char *
mjv_source_get_name (const struct mjv_source *const s)
{
	// No NULL check for s->name; it is NULL if not yet assigned:
	assert(s != NULL);
	return s->name;
}

unsigned int
mjv_source_get_id (const struct mjv_source *const s)
{
	assert(s != NULL);
	return s->id;
}

void
mjv_source_capture (struct mjv_source *s)
{
	int available;
	fd_set fdset;
	struct timespec timeout;

	// Jump table per state; order corresponds with
	// the state enum at the top of this file;
	int (*state_jump_table[])(struct mjv_source *) = {
		state_http_banner,
		state_http_header,
		state_find_boundary,
		state_http_subheader,
		state_find_image,
		state_image_by_content_length,
		state_image_by_eof_search
	};

get_bytes:

	for (;;)
	{
		FD_ZERO(&fdset);
		FD_SET(s->fd, &fdset);
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;
		available = pselect(s->fd + 1, &fdset, NULL, NULL, &timeout, NULL);

		if (available == 0) {
			// Timeout reached:
			break;
		}
		if (available < 0) {
			if (errno == EINTR) {
				continue;
			}
			g_printerr("%s\n", g_strerror(errno));
			break;
		}
		for (;;)
		{
			s->nread = read(s->fd, s->head, BUF_SIZE - (s->head - s->buf));
			if (s->nread < 0) {
				g_printerr("%s\n", g_strerror(errno));
				return; // PREMATURE_EOF
			}
			else if (s->nread == 0) {
				// End of file:
				return;
			}
			// buflast is ONE PAST the real last char:
			s->buflast = s->head + s->nread;

			Debug("Read %u bytes\n", s->nread);

			// Dispatcher; while successful, keep jumping from state to state:
			for (;;)
			{
				switch (state_jump_table[s->state](s))
				{
					case READ_SUCCESS: {
						continue;
					}
					case OUT_OF_BYTES: {
						// Need to read more bytes from the source.

						// If an anchor is defined, then all the data from that point forward
						// should be conserved. So we move the end of the buffer to the start.
						// (This is probably quite inefficient.)
						if (s->anchor != NULL) {
							// Only move if dest and source differ, e.g. the anchor is not
							// already at the start of the buf:
							if (s->anchor != s->buf) {
								memmove(s->buf, s->anchor, s->buflast - s->anchor);
							}
							s->head = s->buf + (s->buflast - s->anchor);
							s->cur  = s->buf + (s->buflast - s->anchor) - 1;
							s->anchor = s->buf;
						}
						// Else if we have no anchor and consumed all bytes in the buffer, start
						// over at the beginning:
						else if (s->cur >= s->buflast) {
							s->cur = s->head = s->buf;
						}
						// Otherwise move all bytes that were still available past s->cur to the
						// front, and append the next ones behind them:
						else {
							memmove(s->buf, s->cur, s->buflast - s->cur);
							s->head = s->buf + (s->buflast - s->cur);
							s->cur = s->buf;
						}
						// Do a tiny sleep of 1/50s to allow more data to come in:
						// usleep(20000);
						goto get_bytes;
					}
					case READ_ERROR: {
						g_printerr("READ_ERROR\n");
						return;
					}
					case CORRUPT_HEADER: {
						g_printerr("Corrupt header\n");
						return;
					}
				}
			}
		}
	}
}

static int
state_http_banner (struct mjv_source *s)
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
		Debug("Corrupt HTTP signature\n");
		return CORRUPT_HEADER;
	}
	// HTML version, accept 1.0 and 1.1:
	if (line_len < 8
	 || (line[7] != '0' && line[7] != '1')) {
		Debug("Corrupt HTTP signature\n");
		return CORRUPT_HEADER;
	}
	// Status code:
	if (line_len < 12
	 || line[8] != ' '
	 || !is_numeric(line[9])
	 || !is_numeric(line[10])
	 || !is_numeric(line[11])) {
		Debug("Corrupt HTTP status code\n");
		return CORRUPT_HEADER;
	}
	// HTML response code in bytes 9..11:
	s->response_code = simple_atoi(&line[9], &line[11]);

	if (s->response_code != 200) {
		// TODO: something better than this:
		Debug("Response code is not 200 but %u\n", s->response_code);
		return READ_ERROR;
	}
	s->state = STATE_HTTP_HEADER;
	return READ_SUCCESS;
}

static int
state_http_header (struct mjv_source *s)
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
				Debug("Could not find boundary\n");
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
#undef STRING_MATCH
	}
	return READ_SUCCESS;
}

static int
state_find_boundary (struct mjv_source *s)
{
	// Loop over the input till we find a \r\n or an \n followed by
	// our boundary marker, and another \r\n or \n:
	DebugEntry();
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
			if ((s->cur - s->anchor) + 1 < (ptrdiff_t)s->boundary_len && *s->cur != s->boundary[s->cur - s->anchor]) {
				s->cur = s->anchor + 1;
				s->anchor = NULL;
			}
			// If successfully found boundary plus one byte, and that byte is \n, then success:
			if ((s->cur - s->anchor) + 1 == (ptrdiff_t)s->boundary_len + 2 && *(s->cur - 1) == 0x0a) {
				s->anchor = NULL;
				s->state = STATE_HTTP_SUBHEADER;
				break;
			}
			// If successfully found boundary plus two bytes...
			if ((s->cur - s->anchor) + 1 == (ptrdiff_t)s->boundary_len + 3) {
				// ..and those two bytes are \r\n, then success...
				if (*(s->cur - 1) == 0x0a && *(s->cur - 2) == 0x0d) {
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
	return READ_SUCCESS;
}

static int
state_http_subheader (struct mjv_source *s)
{
	int ret;
	char *line;
	unsigned int line_len;

	DebugEntry();
	for (;;)
	{
		if ((ret = fetch_header_line(s, &line, &line_len)) != READ_SUCCESS) {
			return ret;
		}
		if (line_len == 0) {
			break;
		}
//		// Print the header:
//		write(2, line, line_len);
//		write(2, "\n", 1);

#define STRING_MATCH(x)	(line_len >= STR_LEN(x) && strncmp(line, x, STR_LEN(x)) == 0)

	//	if (STRING_MATCH(header_content_type_one)
	//	 || STRING_MATCH(header_content_type_two)) {
	//	}
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
#undef STRING_MATCH
	}
	s->state = STATE_FIND_IMAGE;
	return READ_SUCCESS;
}

static int
state_find_image (struct mjv_source *s)
{
	// Consume bytes from the buffer till we find the
	// JPEG signature, which is 0xffd8:
	DebugEntry();
	for (;;)
	{
		if (*s->cur == (char)0xff) {
			s->anchor = s->cur;
		}
		else if ((s->cur - s->anchor) == 1 && *s->cur == (char)0xd8) {
			// s->anchor is already on the 0xff byte,
			// so we've found one byte so far:
			s->byte_counter = 1;

			// If the content length is known, we can use it to
			// take a shortcut; else brute search for the EOF:
			s->state = (s->content_length > 0)
				? STATE_IMAGE_BY_CONTENT_LENGTH
				: STATE_IMAGE_BY_EOF_SEARCH;

			return READ_SUCCESS;
		}
		else {
			s->anchor = NULL;
		}
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
}

static int
state_image_by_content_length (struct mjv_source *s)
{
#define BYTES_LEFT_IN_BUF (s->buflast - s->cur)
#define BYTES_NEEDED ((ptrdiff_t)(s->content_length - s->byte_counter))

	// If we have a content-length > 0, then trust it; read out
	// exactly that many bytes before finding the boundary again.
	DebugEntry();
	for (;;)
	{
		// If more than enough bytes left at end of buffer,
		// we have our image:
		if (BYTES_LEFT_IN_BUF >= BYTES_NEEDED)
		{
			// Skip pointer ahead to end of image:
			s->cur = s->anchor + s->content_length;

			// Report the new frame:
			got_new_frame(s, s->anchor, s->content_length);

			// Reset per-image variables:
			reset_frame_variables(s);

			// Release the anchor:
			s->anchor = NULL;

			// Move to next state, get back to work:
			s->state = STATE_FIND_BOUNDARY;
			return READ_SUCCESS;
		}
		// Else slice off as much as we can and return for more:
		s->byte_counter += BYTES_LEFT_IN_BUF;
		s->cur = s->anchor + s->byte_counter;
		return OUT_OF_BYTES;
	}

#undef BYTES_NEEDED
#undef BYTES_LEFT_IN_BUF
}

static int
state_image_by_eof_search (struct mjv_source *s)
{
	// If no content-length known, then there's nothing we can
	// do but seek the EOF marker, 0xffd9, one byte at a time:
	DebugEntry();
	for (;;)
	{
		// If we found the EOF marker, export the frame and be done:
		if (*s->cur == (char)0xd9 && *(s->cur - 1) == (char)0xff) {
			got_new_frame(s, s->anchor, s->cur - s->anchor + 1);
			reset_frame_variables(s);
			s->state = STATE_FIND_BOUNDARY;
			return READ_SUCCESS;
		}
		// Else increment the current position:
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
increment_cur (struct mjv_source *s)
{
	s->cur++;
	return (s->cur >= s->buflast) ? OUT_OF_BYTES : READ_SUCCESS;
}

static int
fetch_header_line (struct mjv_source *s, char **line, unsigned int *line_len)
{
	// Assume s->cur is on the first character of the line
	// Consume buffer until we hit a line terminator:

#define LINE_LEN (s->cur - s->anchor)

	if (s->anchor == NULL) {
		s->anchor = s->cur;
	}
	for (;;)
	{
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
		// Search for \n's; some cameras do not use the \r\n convention,
		// but plain \n as a line terminator:
		if (*(s->cur - 1) == 0x0a) {
			// If preceded by an 0x0d, count that as a line terminator too:
			if (LINE_LEN >= 2 && *(s->cur - 2) == 0x0d) {
				*line_len = LINE_LEN - 2;
			}
			else {
				*line_len = LINE_LEN - 1;
			}
			*line = s->anchor;
			s->anchor = NULL;
			return READ_SUCCESS;
		}
	}
	// s->cur is left on the last character PAST the line terminator
	// (so the first character of the next line)

#undef LINE_LEN
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
interpret_content_type (struct mjv_source *s, char *line, unsigned int line_len)
{
	char *cur;
	char *last;
	char multipart_x_mixed_replace[] = "multipart/x-mixed-replace";
	char multipart_mixed[] = "multipart/mixed";
	char boundary[] = "boundary=";

	cur = line + STR_LEN(header_content_type_one);
	last = line + line_len - 1;

#define SIZE_LEFT	(last - cur + 1)
#define SKIP_SPACES	while (*cur == ' ') { if (cur++ == last) return CORRUPT_HEADER; }
#define STRING_MATCH(x)	(SIZE_LEFT >= (ptrdiff_t)STR_LEN(x) && strncmp(cur, x, STR_LEN(x)) == 0)

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
					g_printerr("malloc()");
					return READ_ERROR;
				}
				memcpy(s->boundary, cur, s->boundary_len);
				// Zero-terminate:
				s->boundary[s->boundary_len] = 0;
			}
		}
	}
	return READ_SUCCESS;

#undef SIZE_LEFT
#undef SKIP_SPACES
#undef STRING_MATCH

}

static void
reset_frame_variables (struct mjv_source *s)
{
	s->anchor = NULL;
	s->byte_counter = 0;
	s->content_length = 0;
}

static bool
got_new_frame (struct mjv_source *s, char *start, unsigned int len)
{
	struct mjv_frame *frame;

	DebugEntry();

	// Call the callback function, if defined.
	// The callback function assumes responsibility for the mjv_frame pointer.
	if (s->got_frame_callback == NULL) {
		Debug("No callback defined for frame\n");
		return false;
	}
	// Create mjv_frame object:
	if ((frame = mjv_frame_create(start, len)) == NULL) {
		Debug("Could not create frame\n");
		return false;
	}
	// Call the callback function:
	s->got_frame_callback(s, frame);

	return true;
}
