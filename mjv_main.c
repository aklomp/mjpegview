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
	GList *source_list = NULL;

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
	for (GList *link = g_list_first((GList *)config_sources); link; link = g_list_next(link))
	{
		struct mjv_source *source;

		g_printerr("Creating %s\n", mjv_config_source_get_name(MJV_CONFIG_SOURCE(link)));
//		source = mjv_source_create_from_network (
//			mjv_config_source_get_host(MJV_CONFIG_SOURCE(link)),
//			mjv_config_source_get_port(MJV_CONFIG_SOURCE(link)),
//			mjv_config_source_get_path(MJV_CONFIG_SOURCE(link)),
//			mjv_config_source_get_user(MJV_CONFIG_SOURCE(link)),
//			mjv_config_source_get_pass(MJV_CONFIG_SOURCE(link))
//		);
		source = mjv_source_create_from_file("streams/stream.dannycam", 100000);
		if (source == NULL) {
			g_printerr("Could not create source\n");
			return 1;
		}
		if (mjv_source_set_name(source, mjv_config_source_get_name(MJV_CONFIG_SOURCE(link))) == 0) {
			g_printerr("Could not set name\n");
		}
		source_list = g_list_append(source_list, source);
	}
	mjv_gui_main(argc, argv, source_list);

	g_list_free_full(source_list, (GDestroyNotify )mjv_source_destroy);

	mjv_config_destroy(config);

	return 0;
}
