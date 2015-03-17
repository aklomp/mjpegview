#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "source.h"

bool
source_init (
	struct source *s,
	const char *const name,
	bool    (*open)(struct source *),
	ssize_t (*read)(struct source *, void *buf, size_t bufsize),
	void    (*close)(struct source *),
	void    (*destroy)(struct source **))
{
	const char *n = (name == NULL) ? "(unnamed)" : name;

	if ((s->name = strdup(n)) == NULL) {
		return false;
	}
	s->open = open;
	s->read = read;
	s->close = close;
	s->destroy = destroy;
	return true;
}

void
source_deinit (struct source *s)
{
	if (s != NULL) {
		free(s->name);
	}
}

const char *
source_get_name (struct source *const s)
{
	return s->name;
}
