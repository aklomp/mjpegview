#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libconfig.h>

#include "mjv_config.h"

#define malloc_fail(s)   ((s = malloc(sizeof(*(s)))) == NULL)


struct mjv_config {
	config_t *config;
	GList *sources;
};

struct mjv_config_source {
	char *name;
	int   type;
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
	config_init(c->config);
	return c;
}

void
mjv_config_destroy (struct mjv_config *const c)
{
	config_destroy(c->config);
	g_list_free_full(c->sources, (GDestroyNotify)mjv_config_source_destroy);
	free(c->config);
	free(c);
}

#define ADD_STRING(x,y) \
	if (x == NULL) s->x = NULL; \
	else { \
		size_t len = strlen(x) + 1; \
		if ((s->x = malloc(len)) == NULL) { \
			goto err_##y; \
		} \
		memcpy(s->x, x, len); \
	}

struct mjv_config_source *
mjv_config_source_create_from_file (const char *const name, const char *const file, const int usec)
{
	struct mjv_config_source *s;

	if (malloc_fail(s)) {
		goto err_0;
	}
	s->type = TYPE_FILE;
	s->usec = usec;
	ADD_STRING(name, 1);
	ADD_STRING(file, 2);
	return s;

err_2:	free(s->name);
err_1:	free(s);
err_0:	return NULL;
}

struct mjv_config_source *
mjv_config_source_create_from_network (const char *const name, const char *const host, const char *const path, const char *const user, const char *const pass, const int port)
{
	struct mjv_config_source *s;

	if (malloc_fail(s)) {
		goto err_0;
	}
	s->type = TYPE_NETWORK;
	s->port = port;
	ADD_STRING(name, 1);
	ADD_STRING(host, 2);
	ADD_STRING(path, 3);
	ADD_STRING(user, 4);
	ADD_STRING(pass, 5);
	return s;

err_5:	free(s->user);
err_4:	free(s->path);
err_3:	free(s->host);
err_2:	free(s->name);
err_1:	free(s);
err_0:	return NULL;
}

#undef ADD_STRING

void
mjv_config_source_destroy (struct mjv_config_source *const s)
{
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
	free(s);
}

const char *mjv_config_source_get_name (const struct mjv_config_source *const s) { return s->name; }
const char *mjv_config_source_get_file (const struct mjv_config_source *const s) { return s->file; }
const char *mjv_config_source_get_host (const struct mjv_config_source *const s) { return s->host; }
const char *mjv_config_source_get_path (const struct mjv_config_source *const s) { return s->path; }
const char *mjv_config_source_get_user (const struct mjv_config_source *const s) { return s->user; }
const char *mjv_config_source_get_pass (const struct mjv_config_source *const s) { return s->pass; }
int mjv_config_source_get_type (const struct mjv_config_source *const s) { return s->type; }
int mjv_config_source_get_port (const struct mjv_config_source *const s) { return s->port; }
int mjv_config_source_get_usec (const struct mjv_config_source *const s) { return s->usec; }

bool
mjv_config_read_file (struct mjv_config *const c, const char *const filename)
{
	if (config_read_file(c->config, filename) != CONFIG_FALSE) {
		return true;
	}
	g_printerr("%s: %d: %s\n",
		config_error_file(c->config),
		config_error_line(c->config),
		config_error_text(c->config)
	);
	return false;
}

const GList const *
mjv_config_get_sources (struct mjv_config *const c)
{
	unsigned int i;
	unsigned int len;
	config_setting_t *sources;

	if ((sources = config_lookup(c->config, "sources")) == NULL) {
		return NULL;
	}
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
		struct mjv_config_source *mjv_config_source;
		config_setting_t *source = config_setting_get_elem(sources, i);

		if (config_setting_lookup_string(source, "type", &type) == CONFIG_FALSE) {
			continue;
		}
		if (strcmp(type, "file") == 0)
		{
			config_setting_lookup_int(   source, "usec", &usec);
			config_setting_lookup_string(source, "name", &name);
			config_setting_lookup_string(source, "file", &file);

			if ((mjv_config_source = mjv_config_source_create_from_file(name, file, usec)) == NULL) {
				// TODO error handling
				continue;
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

			if ((mjv_config_source = mjv_config_source_create_from_network(name, host, path, user, pass, port)) == NULL) {
				// TODO error handling
				continue;
			}
		}
		else {
			continue;
		}
		c->sources = g_list_append(c->sources, mjv_config_source);
	}
	return c->sources;
}
