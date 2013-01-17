#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>

#include "mjv_log.h"
#include "mjv_source.h"
#include "mjv_config.h"

#define malloc_fail(s)   ((s = malloc(sizeof(*(s)))) == NULL)

struct slist {
	struct mjv_source *source;
	struct slist *next;
};

struct mjv_config {
	config_t *config;
	struct slist *sources;		// first element of sources list
	struct slist *iter;		// current iter pos in sources list
};

struct mjv_config *
mjv_config_init (void)
{
	struct mjv_config *c;

	if (malloc_fail(c)) {
		return NULL;
	}
	if (malloc_fail(c->config)) {
		free(c);
		return NULL;
	}
	c->sources = NULL;
	c->iter = NULL;
	config_init(c->config);
	return c;
}

struct mjv_config *
mjv_config_create_from_file (const char *const filename)
{
	struct mjv_config *c;

	if ((c = mjv_config_init()) == NULL) {
		return NULL;
	}
	if (!mjv_config_read_file(c, filename)) {
		mjv_config_destroy(&c);
		return NULL;
	}
	if (!mjv_config_get_sources(c)) {
		mjv_config_destroy(&c);
		return NULL;
	}
	return c;
}

void
mjv_config_destroy (struct mjv_config **const c)
{
	struct slist *s;
	struct slist *t;

	config_destroy((*c)->config);

	for (s = (*c)->sources; s; s = t) {
		t = s->next;
		mjv_source_destroy(&s->source);
		free(s);
	}
	free((*c)->config);
	free(*c);
	*c = NULL;
}

bool
mjv_config_read_file (struct mjv_config *const c, const char *const filename)
{
	if (config_read_file(c->config, filename) != CONFIG_FALSE) {
		return true;
	}
	log_error("%s: %d: %s\n",
		config_error_file(c->config),
		config_error_line(c->config),
		config_error_text(c->config)
	);
	return false;
}

bool
mjv_config_get_sources (struct mjv_config *const c)
{
	unsigned int i;
	unsigned int len;
	struct slist *t;
	config_setting_t *sources;

	if ((sources = config_lookup(c->config, "sources")) == NULL) {
		return false;
	}
	c->iter = c->sources;
	len = config_setting_length(sources);
	for (i = 0; i < len; i++) {
		int port = 0;
		int usec = 200000;
		const char *type = NULL;
		const char *name = NULL;
		const char *host = NULL;
		const char *path = NULL;
		const char *user = NULL;
		const char *pass = NULL;
		const char *file = NULL;
		struct mjv_source *mjv_source;
		struct slist *s;
		config_setting_t *source = config_setting_get_elem(sources, i);

		if (config_setting_lookup_string(source, "type", &type) == CONFIG_FALSE) {
			continue;
		}
		if (strcmp(type, "file") == 0)
		{
			config_setting_lookup_int(   source, "usec", &usec);
			config_setting_lookup_string(source, "name", &name);
			config_setting_lookup_string(source, "file", &file);

			if ((mjv_source = mjv_source_create_from_file(name, file, usec)) == NULL) {
				goto err;
			}
		}
		else if (strcmp(type, "network") == 0)
		{
			config_setting_lookup_int(   source, "port", &port);
			config_setting_lookup_string(source, "name", &name);
			config_setting_lookup_string(source, "host", &host);
			config_setting_lookup_string(source, "path", &path);
			config_setting_lookup_string(source, "user", &user);
			config_setting_lookup_string(source, "pass", &pass);

			if ((mjv_source = mjv_source_create_from_network(name, host, path, user, pass, port)) == NULL) {
				goto err;
			}
		}
		else {
			continue;
		}
		// Allocate new node for linked list:
		if ((s = malloc(sizeof(*s))) == NULL) {
			mjv_source_destroy(&mjv_source);
			goto err;
		}
		s->source = mjv_source;
		s->next = NULL;
		if (c->iter == NULL) {
			c->sources = s;
		}
		else {
			c->iter->next = s;
		}
		c->iter = s;
	}
	return true;

err:	// Destroy all sources found thus far:
	for (c->iter = c->sources; c->iter; c->iter = t) {
		t = c->iter->next;
		mjv_source_destroy(&c->iter->source);
		free(c->iter);
	}
	return false;
}

struct mjv_source *
mjv_config_source_first (struct mjv_config *c)
{
	if (c == NULL || c->sources == NULL) {
		return NULL;
	}
	c->iter = c->sources;
	return (c->iter) ? c->iter->source : NULL;
}

struct mjv_source *
mjv_config_source_next (struct mjv_config *c)
{
	if (c == NULL || c->iter == NULL) {
		return NULL;
	}
	c->iter = c->iter->next;
	return (c->iter) ? c->iter->source : NULL;
}
