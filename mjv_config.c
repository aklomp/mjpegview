#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include <libconfig.h>
#include "mjv_config.h"

#define g_malloc_fail(s)   ((s = g_try_malloc(sizeof(*(s)))) == NULL)


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

	if (g_malloc_fail(c)) {
		return NULL;
	}
	if (g_malloc_fail(c->config)) {
		g_free(c);
		return NULL;
	}
	c->sources = NULL;
	config_init(c->config);
	return c;
}

void
mjv_config_destroy (struct mjv_config *c)
{
	g_assert(c != NULL);
	g_assert(c->config != NULL);

	config_destroy(c->config);
	g_list_free_full(c->sources, (GDestroyNotify)mjv_config_source_destroy);
	g_free(c->config);
	g_free(c);
}

#define ADD_STRING(x,y) \
	if (x == NULL) s->x = NULL; else { \
		int len = strlen(x) + 1; \
		if ((s->x = g_try_malloc(len)) == NULL) { \
			goto err_##y; \
		} \
		memcpy(s->x, x, len); \
	}

struct mjv_config_source *
mjv_config_source_create_from_file (const char *name, const char *file, int usec)
{
	struct mjv_config_source *s;

	if (g_malloc_fail(s)) {
		goto err_0;
	}
	s->type = TYPE_FILE;
	s->usec = usec;
	ADD_STRING(name, 1);
	ADD_STRING(file, 2);
	return s;

err_2:	g_free(s->name);
err_1:	g_free(s);
err_0:	return NULL;
}

struct mjv_config_source *
mjv_config_source_create_from_network (const char *name, const char *host, const char *path, const char *user, const char *pass, int port)
{
	struct mjv_config_source *s;

	if (g_malloc_fail(s)) {
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

err_5:	g_free(s->user);
err_4:	g_free(s->path);
err_3:	g_free(s->host);
err_2:	g_free(s->name);
err_1:	g_free(s);
err_0:	return NULL;

}

#undef ADD_STRING

void
mjv_config_source_destroy (struct mjv_config_source *s)
{
	g_assert(s != NULL);

	if (s->type == TYPE_NETWORK) {
		g_free(s->name);
		g_free(s->host);
		g_free(s->path);
		g_free(s->user);
		g_free(s->pass);
	}
	else if (s->type == TYPE_FILE) {
		g_free(s->name);
		g_free(s->file);
	}
	g_free(s);
}

const char *mjv_config_source_get_name (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->name; }
const char *mjv_config_source_get_file (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->file; }
const char *mjv_config_source_get_host (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->host; }
const char *mjv_config_source_get_path (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->path; }
const char *mjv_config_source_get_user (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->user; }
const char *mjv_config_source_get_pass (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->pass; }
int mjv_config_source_get_type (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->type; }
int mjv_config_source_get_port (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->port; }
int mjv_config_source_get_usec (const struct mjv_config_source *const s) { g_assert(s != NULL); return s->usec; }

bool
mjv_config_read_file (struct mjv_config *c, const char *filename)
{
	if (config_read_file(c->config, filename) == CONFIG_FALSE) {
		g_printerr("%s: %i: %s\n",
			config_error_file(c->config),
			config_error_line(c->config),
			config_error_text(c->config)
		);
		return false;
	}
	return true;
}

const GList const *
mjv_config_get_sources (struct mjv_config *c)
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
