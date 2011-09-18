#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include <libconfig.h>

struct mjv_config {
	config_t *config;
	GList *sources;
};

struct mjv_config_source {
	char *name;
	char *host;
	char *path;
	char *user;
	char *pass;
	int port;
};

static struct mjv_config_source *mjv_config_source_create (const char *, const char *, const char *, const char *, const char *, int);
static void mjv_config_source_destroy (struct mjv_config_source *);

struct mjv_config *
mjv_config_init (void)
{
	struct mjv_config *c;

	if ((c = g_try_malloc(sizeof(*c))) == NULL) {
		return NULL;
	}
	if ((c->config = g_try_malloc(sizeof(config_t))) == NULL) {
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

static struct mjv_config_source *
mjv_config_source_create (const char *name, const char *host, const char *path, const char *user, const char *pass, int port)
{
	struct mjv_config_source *s;

	if ((s = g_try_malloc(sizeof(*s))) == NULL) {
		goto err_0;
	}

#define ADD_STRING(x,y) \
	if (x == NULL) s->x = NULL; else { \
		int len = strlen(x) + 1; \
		if ((s->x = g_try_malloc(len)) == NULL) { \
			goto err_##y; \
		} \
		memcpy(s->x, x, len); \
	}

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

#undef ADD_STRING
}

static void
mjv_config_source_destroy (struct mjv_config_source *s)
{
	g_assert(s != NULL);

	if (s->name != NULL) g_free(s->name);
	if (s->host != NULL) g_free(s->host);
	if (s->path != NULL) g_free(s->path);
	if (s->user != NULL) g_free(s->user);
	if (s->pass != NULL) g_free(s->pass);
	g_free(s);
}

const char *mjv_config_source_get_name (struct mjv_config_source *s) { g_assert(s != NULL); return s->name; }
const char *mjv_config_source_get_host (struct mjv_config_source *s) { g_assert(s != NULL); return s->host; }
const char *mjv_config_source_get_path (struct mjv_config_source *s) { g_assert(s != NULL); return s->path; }
const char *mjv_config_source_get_user (struct mjv_config_source *s) { g_assert(s != NULL); return s->user; }
const char *mjv_config_source_get_pass (struct mjv_config_source *s) { g_assert(s != NULL); return s->pass; }
      int   mjv_config_source_get_port (struct mjv_config_source *s) { g_assert(s != NULL); return s->port; }

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
		const char *name = NULL;
		const char *host = NULL;
		const char *path = NULL;
		const char *user = NULL;
		const char *pass = NULL;
		struct mjv_config_source *mjv_config_source;
		config_setting_t *source = config_setting_get_elem(sources, i);

		config_setting_lookup_int(source, "port", &port);
		config_setting_lookup_string(source, "name", &name);
		config_setting_lookup_string(source, "host", &host);
		config_setting_lookup_string(source, "path", &path);
		config_setting_lookup_string(source, "user", &user);
		config_setting_lookup_string(source, "pass", &pass);

		if ((mjv_config_source = mjv_config_source_create(name, host, path, user, pass, port)) == NULL) {
			// TODO real error handling
			g_printerr("Could not create object!\n");
			break;
		}
		c->sources = g_list_append(c->sources, mjv_config_source);
	}
	return c->sources;
}
