#include <stdbool.h>
#include <glib.h>
#include <libconfig.h>

struct mjv_config {
	config_t *config;
};

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
	config_init(c->config);
	return c;
}

void
mjv_config_destroy (struct mjv_config *c)
{
	g_assert(c != NULL);
	g_assert(c->config != NULL);

	config_destroy(c->config);
	g_free(c->config);
	g_free(c);
}

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
