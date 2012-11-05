#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>

#include "mjv_log.h"
#include "mjv_source.h"

struct mjv_source {
	char *name;
	int   type;
	int   fd;
	union {
		struct {
			int   port;
			char *host;
			char *path;
			char *user;
			char *pass;
		};
		struct {
			char *file;
			unsigned int usec;
		};
	};
};

static bool set_default_name (struct mjv_source *s);
static bool open_file (struct mjv_source *s);
static bool open_network (struct mjv_source *s);
static bool write_http_request (struct mjv_source *s);
static bool write_auth_string (struct mjv_source *s);
static void base64_encode (char *const src, size_t srclen, char *const dst);

static char err_write_failed[] = "Write failed\n";
static char err_malloc_failed[] = "malloc() failed\n";

#define malloc_fail(s)   ((s = malloc(sizeof(*(s)))) == NULL)

#define ADD_STRING(x,y) \
	if (x == NULL) s->x = NULL; \
	else { \
		size_t len = strlen(x) + 1; \
		if ((s->x = malloc(len)) == NULL) { \
			log_error(err_malloc_failed); \
			goto err_##y; \
		} \
		memcpy(s->x, x, len); \
	}

// The string length of a constant character array is one less
// than its apparent size, because of the zero terminator:
#define STR_LEN(x)	(sizeof(x) - 1)

#define SAFE_WRITE(x, y) \
	{ \
		int write_size = y; \
		if (write(s->fd, x, write_size) != write_size) { \
			log_error(err_write_failed); \
			ret = 0; \
			goto err; \
		} \
	}

#define SAFE_WRITE_STR(x) \
		SAFE_WRITE(x, STR_LEN(x))

struct mjv_source *
mjv_source_create_from_file (const char *const name, const char *const file, const int usec)
{
	struct mjv_source *s;

	if (malloc_fail(s)) {
		goto err_0;
	}
	s->type = TYPE_FILE;
	s->usec = usec;
	s->fd = -1;
	ADD_STRING(name, 1);
	ADD_STRING(file, 2);
	if (s->name == NULL && set_default_name(s) == 0) {
		goto err_3;
	}
	return s;

err_3:	free(s->file);
err_2:	free(s->name);
err_1:	free(s);
err_0:	return NULL;
}

struct mjv_source *
mjv_source_create_from_network (const char *const name, const char *const host, const char *const path, const char *const user, const char *const pass, const int port)
{
	struct mjv_source *s;

	if (malloc_fail(s)) {
		goto err_0;
	}
	s->type = TYPE_NETWORK;
	s->port = port;
	s->fd = -1;
	ADD_STRING(name, 1);
	ADD_STRING(host, 2);
	ADD_STRING(path, 3);
	ADD_STRING(user, 4);
	ADD_STRING(pass, 5);
	if (s->name == NULL && set_default_name(s) == 0) {
		goto err_6;
	}
	return s;

err_6:	free(s->pass);
err_5:	free(s->user);
err_4:	free(s->path);
err_3:	free(s->host);
err_2:	free(s->name);
err_1:	free(s);
err_0:	return NULL;
}

void
mjv_source_destroy (struct mjv_source *const s)
{
	if (s == NULL) {
		return;
	}
	if (s->type == TYPE_NETWORK) {
		free(s->name);
		free(s->host);
		free(s->path);
		free(s->user);
		free(s->pass);
	}
	else if (s->type == TYPE_FILE) {
		free(s->name);
		free(s->file);
	}
	if (s->fd >= 0) {
		close(s->fd);
	}
	free(s);
}

const char *mjv_source_get_name (const struct mjv_source *const s) { return s->name; }
const char *mjv_source_get_file (const struct mjv_source *const s) { return s->file; }
const char *mjv_source_get_host (const struct mjv_source *const s) { return s->host; }
const char *mjv_source_get_path (const struct mjv_source *const s) { return s->path; }
const char *mjv_source_get_user (const struct mjv_source *const s) { return s->user; }
const char *mjv_source_get_pass (const struct mjv_source *const s) { return s->pass; }
      int   mjv_source_get_type (const struct mjv_source *const s) { return s->type; }
      int   mjv_source_get_port (const struct mjv_source *const s) { return s->port; }
      int   mjv_source_get_usec (const struct mjv_source *const s) { return s->usec; }
      int   mjv_source_get_fd   (const struct mjv_source *const s) { return s->fd;   }

int
mjv_source_open (struct mjv_source *cs)
{
	switch (cs->type) {
		case TYPE_FILE:    return open_file(cs);
		case TYPE_NETWORK: return open_network(cs);
	}
	return 0;
}

static bool
set_default_name (struct mjv_source *s)
{
	char name[] = "(unnamed)";
	if ((s->name = malloc(sizeof(name))) == NULL) {
		log_error(err_malloc_failed);
		return false;
	}
	memcpy(s->name, name, sizeof(name));
	return true;
}

static bool
open_file (struct mjv_source *s)
{
	if (s->file == NULL) {
		log_error("No filename given\n");
		return false;
	}
	if ((s->fd = open(s->file, O_RDONLY)) < 0) {
		perror("open()");
		return false;
	}
	return true;
}

static bool
open_network (struct mjv_source *s)
{
	char port_str[6];
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	// Validate port:
	if (s->port < 0 || s->port > 65535) {
		log_error("Invalid port\n");
		return false;
	}
	// Validate host:
	if (s->host == NULL) {
		log_error("No host given\n");
		return false;
	}
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_TCP;

	// The second argument of getaddrinfo() is the port number
	// as a string, so snprintf it:
	snprintf(port_str, sizeof(port_str), "%u", s->port);
	if (getaddrinfo(s->host, port_str, &hints, &result) != 0) {
		perror("getaddrinfo()");
		return false;
	}
	// Loop over results, trying them all till we find a descriptor
	// that works:
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		if ((s->fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) {
			continue;
		}
		if (connect(s->fd, rp->ai_addr, rp->ai_addrlen) >= 0) {
			break;
		}
		close(s->fd);
	}
	freeaddrinfo(result);
	if (rp == NULL) {
		s->fd = -1;
		log_error("Could not connect\n");
		return false;
	}
	return write_http_request(s);
}

static bool
write_http_request (struct mjv_source *s)
{
	bool ret = true;
	char *buffer = NULL;
	size_t buffer_len;
	char crlf[] = "\r\n";
	char keep_alive[] = "Connection: Keep-Alive\r\n";

	if ((buffer = malloc(100)) == NULL) {
		log_error(err_malloc_failed);
		ret = false;
		goto err;
	}
	if ((buffer_len = snprintf(buffer, 100, "GET %s HTTP/1.0\r\n", s->path)) >= 100) {
		log_error("snprintf() buffer too short\n");
		ret = false;
		goto err;
	}
	// We have to write header lines in their entirety at once;
	// at least one IP camera closes its connection if it receives
	// a partial line.
	SAFE_WRITE(buffer, buffer_len);
	SAFE_WRITE_STR(keep_alive);

	// Write basic authentication header if credentials available:
	if (s->user != NULL && s->pass != NULL && write_auth_string(s) == 0) {
		ret = false;
		goto err;
	}
	// Finalize header with an extra CRLF:
	SAFE_WRITE_STR(crlf);

err:	free(buffer);
	return ret;
}

static bool
write_auth_string (struct mjv_source *s)
{
	bool ret = true;
	char *header = NULL;
	char *auth_string = NULL;
	char *base64_auth_string = NULL;
	size_t user_len = strlen(s->user);
	size_t pass_len = strlen(s->pass);

	// Caller ensures s->user and s->pass are non-NULL
	// The auth string has the form 'username:password':
	if ((auth_string = malloc(user_len + pass_len + 1)) == NULL) {
		log_error(err_malloc_failed);
		ret = false;
		goto err;
	}
	memcpy(auth_string, s->user, user_len);
	auth_string[user_len] = ':';
	memcpy(auth_string + user_len + 1, s->pass, pass_len);

	// Encode this auth string into base64:
	if ((base64_auth_string = malloc(((user_len + pass_len + 1) * 4) / 3 + 3)) == NULL) {
		log_error(err_malloc_failed);
		ret = false;
		goto err;
	}
	base64_encode(auth_string, user_len + pass_len + 1, base64_auth_string);

	// Print auth header:
	char header_fmt[] = "Authorization: Basic %s\r\n";
	size_t header_len = STR_LEN(header_fmt) - 2 + strlen(base64_auth_string);

	if ((header = malloc(header_len + 1)) == NULL) {
		log_error(err_malloc_failed);
		ret = false;
		goto err;
	}
	snprintf(header, header_len + 1, header_fmt, base64_auth_string);
	SAFE_WRITE(header, header_len);

err:	free(base64_auth_string);
	free(auth_string);
	free(header);
	return ret;
}

static void
base64_encode (char *const src, size_t srclen, char *const dst)
{
	// Assume that *dst is large enough to contain the output.
	// Theoretically it should be 4/3 the length of src.
	char *c = src;
	char *o = dst;

	for (;;)
	{
		char in[4] = { 0, 0, 0, 0 };
		int valid = 1;

		if ((size_t)(c - src) >= srclen) {
			break;
		}
		// Turn three bytes into four 6-bit numbers:
		// in[0] = 00111111
		// in[1] = 00112222
		// in[2] = 00222233
		// in[3] = 00333333

		in[0] = *c >> 2;
		in[1] = (*c & 0x03) << 4;
		if ((size_t)(++c - src) >= srclen) goto out;
		valid++;
		in[1] |= ((*c & 0xF0) >> 4);
		in[2] = (*c & 0x0F) << 2;
		if ((size_t)(++c - src) >= srclen) goto out;
		valid++;
		in[2] |= ((*c >> 6) & 0x03);
		in[3] = *c;
		c++;

out:		for (int i = 0; i < 4; i++) {
			if (i > valid) {
				*o++ = '=';
				continue;
			}
			*o++ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			       "abcdefghijklmnopqrstuvwxyz"
			       "0123456789+/"
			       [(int)(in[i] & 0x3F)];
		}
		if (valid < 3) {
			break;
		}
	}
	*o = '\0';
}
