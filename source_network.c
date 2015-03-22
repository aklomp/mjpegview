#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>

#include "mjv_log.h"
#include "source.h"

static char err_write_failed[] = "Write failed\n";
static char err_malloc_failed[] = "malloc() failed\n";

#define malloc_fail(s)   ((s = malloc(sizeof(*(s)))) == NULL)

// The string length of a constant character array is one less
// than its apparent size, because of the zero terminator:
#define STR_LEN(x)	(sizeof(x) - 1)

#define SAFE_WRITE(x, y) \
	{ \
		int write_size = y; \
		if (write(sn->source.fd, x, write_size) != write_size) { \
			log_error(err_write_failed); \
			ret = 0; \
			goto err; \
		} \
	}

#define SAFE_WRITE_STR(x) \
		SAFE_WRITE(x, STR_LEN(x))

struct source_network {
	struct source source;
	char *host;
	char *path;
	char *user;
	char *pass;
	int   port;
};

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

static bool
write_auth_string (struct source_network *sn)
{
	bool ret = true;
	size_t user_len = strlen(sn->user);
	size_t pass_len = strlen(sn->pass);
	char auth_string[user_len + pass_len + 1];
	char base64_auth_string[((user_len + pass_len + 1) * 4) / 3 + 3];

	// Caller ensures s->user and s->pass are non-NULL
	// The auth string has the form 'username:password':
	memcpy(auth_string, sn->user, user_len);
	auth_string[user_len] = ':';
	memcpy(auth_string + user_len + 1, sn->pass, pass_len);

	// Encode this auth string into base64:
	base64_encode(auth_string, user_len + pass_len + 1, base64_auth_string);

	// Print auth header:
	char header_fmt[] = "Authorization: Basic %s\r\n";
	size_t header_len = STR_LEN(header_fmt) - 2 + strlen(base64_auth_string);
	char header[header_len + 1];

	snprintf(header, header_len + 1, header_fmt, base64_auth_string);
	SAFE_WRITE(header, header_len);

err:	return ret;
}

static bool
write_http_request (struct source_network *sn)
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
	if ((buffer_len = snprintf(buffer, 100, "GET %s HTTP/1.0\r\n", sn->path)) >= 100) {
		ret = false;
		goto err;
	}
	// We have to write header lines in their entirety at once;
	// at least one IP camera closes its connection if it receives
	// a partial line.
	SAFE_WRITE(buffer, buffer_len);
	SAFE_WRITE_STR(keep_alive);

	// Write basic authentication header if credentials available:
	if (sn->user != NULL && sn->pass != NULL && write_auth_string(sn) == 0) {
		ret = false;
		goto err;
	}
	// Finalize header with an extra CRLF:
	SAFE_WRITE_STR(crlf);

err:	free(buffer);
	return ret;
}

static bool
open_network (struct source *s)
{
	struct source_network *sn = (struct source_network *)s;

	char port_str[6];
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	// Validate port:
	if (sn->port < 0 || sn->port > 65535) {
		log_error("Invalid port: %d\n", sn->port);
		return false;
	}
	// Validate host:
	if (sn->host == NULL) {
		log_error("No host\n");
		return false;
	}
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_TCP;

	// The second argument of getaddrinfo() is the port number
	// as a string, so snprintf it:
	snprintf(port_str, sizeof(port_str), "%u", sn->port);
	if (getaddrinfo(sn->host, port_str, &hints, &result) != 0) {
		log_error("getaddrinfo()");
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
		return false;
	}
	return write_http_request(sn);
}

static void
close_network (struct source *s)
{
	if (s->fd >= 0) {
		close(s->fd);
		s->fd = -1;
	}
}

static void
source_network_destroy (struct source **s)
{
	struct source_network **sn = (struct source_network **)s;

	if (sn == NULL || *sn == NULL) {
		return;
	}
	free((*sn)->pass);
	free((*sn)->user);
	free((*sn)->path);
	free((*sn)->host);
	source_deinit(&(*sn)->source);
	free(*sn);
	*sn = NULL;
}

struct source *
source_network_create (
	const char *const name,
	const char *const host,
	const char *const path,
	const char *const user,
	const char *const pass,
	const int port)
{
	struct source_network *sn;

	// Allocate memory:
	if ((sn = malloc(sizeof(*sn))) == NULL) {
		goto err0;
	}
	// Init the generic Source part:
	if (source_init(&sn->source, name, open_network, close_network, source_network_destroy) == false) {
		goto err1;
	}
	sn->host = NULL;
	sn->path = NULL;
	sn->user = NULL;
	sn->pass = NULL;

	// Copy strings:
	if (host != NULL && (sn->host = strdup(host)) == NULL) {
		goto err2;
	}
	if (path != NULL && (sn->path = strdup(path)) == NULL) {
		goto err3;
	}
	if (user != NULL && (sn->user = strdup(user)) == NULL) {
		goto err4;
	}
	if (pass != NULL && (sn->pass = strdup(pass)) == NULL) {
		goto err5;
	}
	sn->port = port;
	sn->source.fd = -1;
	return &sn->source;

err5:	free(sn->user);
err4:	free(sn->path);
err3:	free(sn->host);
err2:	source_deinit(&sn->source);
err1:	free(sn);
err0:	return NULL;
}
