#include <stdbool.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "mjv_config.h"
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_gui.h"

#define MJV_CONFIG_SOURCE(x)  ((struct mjv_config_source *)((x)->data))

static const char config_file[] = "config";

int
main (int argc, char **argv)
{
	GList *config_sources = NULL;
	GList *sources = NULL;

	struct mjv_config *config;

	if ((config = mjv_config_init()) == NULL) {
		g_printerr("Fatal: could not load config\n");
		return 1;
	}
	if (mjv_config_read_file(config, config_file) == 0) {
		// FIXME app should work without a config file
		g_printerr("Fatal: could not read config\n");
		return 1;
	}
	if ((config_sources = (GList *)mjv_config_get_sources(config)) == NULL) {
		g_printerr("Fatal: could not get config list of sources\n");
		return 1;
	}
	// Loop over all config sources, create a source for them:
	for (GList *link = g_list_first(config_sources); link; link = g_list_next(link)) {
		struct mjv_source *source;

		g_printerr("%s\n", mjv_config_source_get_name(MJV_CONFIG_SOURCE(link)));
		source = mjv_source_create_from_network (
			mjv_config_source_get_host(MJV_CONFIG_SOURCE(link)),
			mjv_config_source_get_port(MJV_CONFIG_SOURCE(link)),
			mjv_config_source_get_path(MJV_CONFIG_SOURCE(link)),
			mjv_config_source_get_user(MJV_CONFIG_SOURCE(link)),
			mjv_config_source_get_pass(MJV_CONFIG_SOURCE(link)),
			mjv_gui_show_frame
		);
		if (source == NULL) {
			g_printerr("Could not create source\n");
			return 1;
		}
		if (mjv_source_set_name(source, mjv_config_source_get_name(MJV_CONFIG_SOURCE(link))) == 0) {
			g_printerr("Could not set name\n");
		}
		sources = g_list_append(sources, source);
	}
	mjv_gui_main(argc, argv, sources);

	g_list_free_full(sources, (GDestroyNotify)mjv_source_destroy);

	mjv_config_destroy(config);

	return 0;
}
