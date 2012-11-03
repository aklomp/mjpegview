#include <stdbool.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "mjv_log.h"
#include "mjv_config_source.h"
#include "mjv_config.h"
#include "mjv_frame.h"
#include "mjv_gui.h"

#define MJV_CONFIG_SOURCE(x)  ((struct mjv_config_source *)((x)->data))

static const char config_file[] = "config";

int
main (int argc, char **argv)
{
	GList *config_sources_list = NULL;
	struct mjv_config *config;

	if ((config = mjv_config_init()) == NULL) {
		log_error("Fatal: could not load config\n");
		return 1;
	}
	if (mjv_config_read_file(config, config_file) == 0) {
		// FIXME app should work without a config file
		log_error("Fatal: could not read config\n");
		return 1;
	}
	if ((config_sources_list = (GList *)mjv_config_get_sources(config)) == NULL) {
		log_error("Fatal: could not get config list of sources\n");
		return 1;
	}
	mjv_gui_main(argc, argv, config_sources_list);

	mjv_config_destroy(config);

	return 0;
}
