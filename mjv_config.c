#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>

#include "mjv_log.h"
#include "source.h"
#include "source_file.h"
#include "source_network.h"

struct slist {
	struct source *source;
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

	if ((c = malloc(sizeof(*c))) == NULL) {
		return NULL;
	}
	if ((c->config = malloc(sizeof(*c->config))) == NULL) {
		free(c);
		return NULL;
	}
	c->sources = NULL;
	c->iter = NULL;
	config_init(c->config);
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
		s->source->destroy(&s->source);
		free(s);
	}
	free((*c)->config);
	free(*c);
	*c = NULL;
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
		struct source *source;
		struct slist *s;
		config_setting_t *csource = config_setting_get_elem(sources, i);

		if (config_setting_lookup_string(csource, "type", &type) == CONFIG_FALSE) {
			continue;
		}
		if (strcmp(type, "file") == 0)
		{
			config_setting_lookup_int(   csource, "usec", &usec);
			config_setting_lookup_string(csource, "name", &name);
			config_setting_lookup_string(csource, "file", &file);

			if ((source = source_file_create(name, file, usec)) == NULL) {
				goto err;
			}
		}
		else if (strcmp(type, "network") == 0)
		{
			config_setting_lookup_int(   csource, "port", &port);
			config_setting_lookup_string(csource, "name", &name);
			config_setting_lookup_string(csource, "host", &host);
			config_setting_lookup_string(csource, "path", &path);
			config_setting_lookup_string(csource, "user", &user);
			config_setting_lookup_string(csource, "pass", &pass);

			if ((source = source_network_create(name, host, path, user, pass, port)) == NULL) {
				goto err;
			}
		}
		else {
			continue;
		}
		// Allocate new node for linked list:
		if ((s = malloc(sizeof(*s))) == NULL) {
			source->destroy(&source);
			goto err;
		}
		s->source = source;
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
		c->iter->source->destroy(&c->iter->source);
		free(c->iter);
	}
	return false;
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

struct source *
mjv_config_source_first (struct mjv_config *c)
{
	if (c == NULL || c->sources == NULL) {
		return NULL;
	}
	c->iter = c->sources;
	return (c->iter) ? c->iter->source : NULL;
}

struct source *
mjv_config_source_next (struct mjv_config *c)
{
	if (c == NULL || c->iter == NULL) {
		return NULL;
	}
	c->iter = c->iter->next;
	return (c->iter) ? c->iter->source : NULL;
}
