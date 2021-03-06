#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "source.h"

struct source_file {
	struct source source;
	char *path;
	int usec;
};

static bool
open_file (struct source *s)
{
	struct source_file *sf = (struct source_file *)s;

	if (sf->path == NULL) {
		return false;
	}
	if ((s->fd = open(sf->path, O_RDONLY)) < 0) {
		return false;
	}
	return true;
}

static void
close_file (struct source *s)
{
	if (s->fd >= 0) {
		close(s->fd);
		s->fd = -1;
	}
}

static void
source_file_destroy (struct source **s)
{
	struct source_file **sf = (struct source_file **)s;

	if (sf == NULL || *sf == NULL) {
		return;
	}
	source_deinit(&(*sf)->source);
	free((*sf)->path);
	free(*sf);
	*sf = NULL;
}

struct source *
source_file_create (const char *const name, const char *const path, const int usec)
{
	struct source_file *sf;

	// Allocate memory for the structure:
	if ((sf = malloc(sizeof(*sf))) == NULL) {
		goto err0;
	}
	// Init the generic Source part:
	if (source_init(&sf->source, name, open_file, close_file, source_file_destroy) == false) {
		goto err1;
	}
	// Duplicate path:
	if ((sf->path = strdup(path)) == NULL) {
		goto err2;
	}
	sf->source.fd = -1;
	sf->usec = usec;
	return &sf->source;

err2:	source_deinit(&sf->source);
err1:	free(sf);
err0:	return NULL;
}
