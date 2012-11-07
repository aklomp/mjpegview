#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libconfig.h>

#include "mjv_log.h"
#include "mjv_source.h"
#include "mjv_config.h"

#define malloc_fail(s)   ((s = malloc(sizeof(*(s)))) == NULL)


struct mjv_config {
	config_t *config;
	GList *sources;
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
	GList *link = NULL;

	config_destroy(c->config);

	for (link = g_list_first(c->sources); link; link = g_list_next(link)) {
		mjv_source_destroy((struct mjv_source **)(&link->data));
	}
	g_list_free(c->sources);
	free(c->config);
	free(c);
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

GList *
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
		struct mjv_source *mjv_source;
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

			if ((mjv_source = mjv_source_create_from_network(name, host, path, user, pass, port)) == NULL) {
				// TODO error handling
				continue;
			}
		}
		else {
			continue;
		}
		c->sources = g_list_append(c->sources, mjv_source);
	}
	return c->sources;
}
