#include <stdbool.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>	// close()
#include <fcntl.h>
#include <stdlib.h>	// malloc(), free()
#include <string.h>	// strncmp(), memcpy()
#include <glib.h>
#include <glib/gprintf.h>
#include <errno.h>
#include <netdb.h>	// hints
#include <time.h>	// clock_gettime()
#include <unistd.h>	// open(), write(), close()
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>

#include "mjv_log.h"
#include "mjv_config.h"
#include "mjv_frame.h"
#include "mjv_framebuf.h"
#include "mjv_source.h"

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

// Debug print functions:
#if 0
  #define Debug(x...)	log_debug(x)
  #define DebugEntry()	log_debug("Entering %s\n", __func__)
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
	int nread;		// return value of read();
	int mimetype;
	int state;		// state machine state
	char *boundary;
	int delay_usec;
	unsigned int id;
	unsigned int boundary_len;
	unsigned int response_code;
	unsigned int content_length;
	struct timespec last_emitted;
	struct mjv_framebuf *framebuf;
	struct mjv_config_source *config;

	char *buf;	// read buffer;
	char *cur;	// current char under inspection in buffer;
	char *head;	// where the current read starts;
	char *anchor;	// the first byte in the buffer to keep;

	// This callback function is called whenever
	// a mjv_frame object is created by a source:
	void (*callback)(struct mjv_frame *, void *);
	void *user_pointer;
};

static bool mjv_source_open_file (struct mjv_source *);
static bool mjv_source_open_network (struct mjv_source *);
static int fetch_header_line (struct mjv_source *, char **, unsigned int *);
static inline int increment_cur (struct mjv_source *);
static inline bool is_numeric (char);
static inline unsigned int simple_atoi (const char *, const char *);
static int interpret_content_type (struct mjv_source *, char *, unsigned int);
static bool got_new_frame (struct mjv_source *, char *, unsigned int);
static void artificial_delay (unsigned int, struct timespec *);

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
	GString *buffer = NULL;
	char *auth_string = NULL;
	gchar *base64_auth_string = NULL;
	char crlf[] = "\r\n";
	char keep_alive[] = "Connection: Keep-Alive\r\n";

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

	unsigned int username_len = (username == NULL) ? 0 : strlen(username);
	unsigned int password_len = (password == NULL) ? 0 : strlen(password);

	// We have to write entire header lines at a time; at least one IP
	// camera closes its connection if it receives only a GET and then
	// a pause between the next write.

	buffer = g_string_sized_new(80);
	g_string_printf(buffer, "GET %s HTTP/1.0\r\n", path);
	SAFE_WRITE(buffer->str, buffer->len);
	SAFE_WRITE_STR(keep_alive);

	// Add basic authentication header if credentials passed:
	if (username != NULL && password != NULL)
	{
		// The auth string is 'username:password':
		if ((auth_string = malloc(username_len + password_len + 1)) == NULL) {
			goto err;
		}
		memcpy(auth_string, username, username_len);
		auth_string[username_len] = ':';
		memcpy(auth_string + username_len + 1, password, password_len);
		base64_auth_string = g_base64_encode((guchar *)auth_string, (gsize)(username_len + password_len + 1));
		g_string_printf(buffer, "Authorization: Basic %s\r\n", base64_auth_string);
		SAFE_WRITE(buffer->str, buffer->len);
		g_free(base64_auth_string);
		free(auth_string);
		base64_auth_string = auth_string = NULL;
	}
	SAFE_WRITE_STR(crlf);
	g_string_free(buffer, TRUE);
	return true;

#undef SAFE_WRITE_STR
#undef SAFE_WRITE

err:	g_free(base64_auth_string);
	free(auth_string);
	g_string_free(buffer, TRUE);
	return false;
}

bool
mjv_source_open (struct mjv_source *const s)
{
	switch (mjv_config_source_get_type(s->config))
	{
		case TYPE_FILE:    return mjv_source_open_file(s);
		case TYPE_NETWORK: return mjv_source_open_network(s);
	}
	return false;
}

static bool
mjv_source_open_file (struct mjv_source *s)
{
	const char *file = mjv_config_source_get_file(s->config);
	s->delay_usec = mjv_config_source_get_usec(s->config);

	if ((s->fd = open(file, O_RDONLY)) < 0) {
		log_error("%s: %s\n", strerror(errno), file);
		return false;
	}
	return true;
}

static bool
mjv_source_open_network (struct mjv_source *s)
{
	int fd = -1;
	char port_string[6];
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	       int  port = mjv_config_source_get_port(s->config);
	const char *host = mjv_config_source_get_host(s->config);
	const char *path = mjv_config_source_get_path(s->config);
	const char *user = mjv_config_source_get_user(s->config);
	const char *pass = mjv_config_source_get_pass(s->config);

	// FIXME: all char* arguments are assumed to be null-terminated,
	// but all of them come from user input! Don't be so trusting.

	Debug("host: %s\n", host);
	Debug("port: %i\n", port);
	Debug("path: %s\n", path);
	Debug("user: %s\n", user);
	Debug("pass: %s\n", pass);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_TCP;

	// Second argument of getaddrinfo() is the port number,
	// as a string; so snprintf() it:
	if (port > 65535) {
		log_error("Implausible port\n");
		goto err;
	}
	g_snprintf(port_string, sizeof(port_string), "%u", port);

	if (getaddrinfo(host, port_string, &hints, &result) != 0) {
		log_error("%s\n", strerror(errno));
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
		log_debug("Could not connect\n");
		goto err;
	}
	Debug("Connected\n");

	// Write the HTTP request:
	if (!write_http_request(fd, path, user, pass)) {
		log_debug("Could not write HTTP request\n");
		goto err;
	}
	log_debug("Wrote http request\n");

	// The response is handled by the stream decoder. We're done:
	s->fd = fd;
	return true;

err:	if (fd >= 0) {
		close(fd);
	}
	return false;
}

struct mjv_source *
mjv_source_create (struct mjv_config_source *config)
{
	int ret;
	struct mjv_source *s = NULL;

	// Allocate memory for the structure:
	if ((s = malloc(sizeof(*s))) == NULL) {
		goto err;
	}
	s->name = NULL;
	if ((s->buf = malloc(BUF_SIZE)) == NULL) {
		goto err;
	}
	// Set default values:
	s->fd = -1;
	s->id = ++last_id;	// First created camera has id #1
	s->boundary = NULL;
	s->framebuf = NULL;
	s->mimetype = -1;
	s->content_length = 0;
	s->delay_usec = 0;
	s->state = STATE_HTTP_BANNER;
	s->last_emitted.tv_sec = 0;
	s->last_emitted.tv_nsec = 0;
	s->config = config;

	s->callback = NULL;
	s->user_pointer = NULL;

	s->anchor = NULL;
	s->cur = s->head = s->buf;

	// Set a default name based on the ID:
	if ((s->name = malloc(15)) == NULL) {
		goto err;
	}
	ret = g_snprintf(s->name, 15, "Camera %u", s->id);
	if (ret < 0) {
		log_error("Could not g_snprintf() the default camera name\n");
		goto err;
	}
	if (ret >= 15) {
		log_error("Not enough space to g_snprintf() the default camera name\n");
		goto err;
	}
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
	if (s->name != NULL) {
		log_info("Destroying source %s\n", s->name);
		free(s->name);
	}
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
	char *c;
	size_t name_len;

	assert(s != NULL);
	assert(name != NULL);

	// Check string length for ridiculousness:
	if ((name_len = strlen(name)) > 100) {
		return 0;
	}
	// Destroy any existing name, reserve memory:
	if ((c = realloc(s->name, name_len + 1)) == NULL) {
		return 0;
	}
	// Copy name plus terminator into memory:
	memcpy(s->name = c, name, name_len + 1);
	return 1;
}

void
mjv_source_set_callback (struct mjv_source *s, void (*got_frame_callback)(struct mjv_frame*, void*), void *user_pointer)
{
	g_assert(s != NULL);
	s->callback = got_frame_callback;
	s->user_pointer = user_pointer;
}

const char *
mjv_source_get_name (const struct mjv_source *const s)
{
	assert(s != NULL);
	return s->name;
}

unsigned int
mjv_source_get_id (const struct mjv_source *const s)
{
	assert(s != NULL);
	return s->id;
}

static void
adjust_streambuf (struct mjv_source *s)
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

	g_assert(s->buf != NULL);
	g_assert(keepfrom != NULL);
	g_assert(s->buf <= keepfrom);
	g_assert(s->head >= keepfrom);

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

enum mjv_source_status
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

	for (;;)
	{
		FD_ZERO(&fdset);
		FD_SET(s->fd, &fdset);
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;

		// Make this thread explicitly cancelable here:
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		// pselect() is a pthread cancellation point. When this thread
		// receives a cancellation request, it will be inside here.
		available = pselect(s->fd + 1, &fdset, NULL, NULL, &timeout, NULL);

		// Make this thread uncancelable; once processing IO,
		// it should be allowed to finish its job.
		// Note: this is fine when all goes well, but not when it doesn't.
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (available == 0) {
			// timeout reached
			log_info("Timeout reached. Giving up.\n");
			return MJV_SOURCE_TIMEOUT;
		}
		if (available < 0) {
			if (errno == EINTR) {
				continue;
			}
			log_error("%s\n", strerror(errno));
			return MJV_SOURCE_READ_ERROR;
		}
		s->nread = read(s->fd, s->head, BUF_SIZE - (s->head - s->buf));
		if (s->nread < 0) {
			log_error("%s\n", strerror(errno));
			return MJV_SOURCE_READ_ERROR;
		}
		else if (s->nread == 0) {
			log_info("End of file\n");
			return MJV_SOURCE_PREMATURE_EOF;
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
				return MJV_SOURCE_READ_ERROR;

			case CORRUPT_HEADER:
				log_error("Corrupt header\n");
				return MJV_SOURCE_CORRUPT_HEADER;
		}
	}
	return MJV_SOURCE_SUCCESS;
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
				log_error("Could not find boundary\n");
				return CORRUPT_HEADER;
			}
			s->state = STATE_FIND_BOUNDARY;
			break;
		}
//		write(1, line, line_len);
//		write(1, "<\n", 2);
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
		if (increment_cur(s) == OUT_OF_BYTES) {
			return OUT_OF_BYTES;
		}
	}
	s->state = STATE_FIND_IMAGE;
	return increment_cur(s);

#undef STRING_MATCH
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
state_image_by_content_length (struct mjv_source *s)
{
#define BYTES_LEFT_IN_BUF (s->head - s->cur - 1)
#define BYTES_FOUND  (s->cur + 1 - s->anchor)
#define BYTES_NEEDED (ptrdiff_t)(s->content_length - BYTES_FOUND)

	// If we have a content-length > 0, then trust it; read out
	// exactly that many bytes before finding the boundary again.
	DebugEntry();
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
state_image_by_eof_search (struct mjv_source *s)
{
	// If no content-length known, then there's nothing we can
	// do but seek the EOF marker, 0xffd9, one byte at a time:
	DebugEntry();
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
increment_cur (struct mjv_source *s)
{
	s->cur++;
	return (s->cur >= s->head) ? OUT_OF_BYTES : READ_SUCCESS;
}

static int
fetch_header_line (struct mjv_source *s, char **line, unsigned int *line_len)
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
interpret_content_type (struct mjv_source *s, char *line, unsigned int line_len)
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
got_new_frame (struct mjv_source *s, char *start, unsigned int len)
{
	struct mjv_frame *frame;

	DebugEntry();

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

	DebugEntry();

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
