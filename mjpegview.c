#include <stdbool.h>
#include <stddef.h>	// NULL

#include "mjv_log.h"
#include "mjv_config.h"
#include "mjv_gui.h"

// TODO: make this configurable:
static char *config_file = "config";

int
main (int argc, char **argv)
{
	struct mjv_config *config;

	if ((config = mjv_config_create_from_file(config_file)) == NULL) {
		log_error("Fatal: could not open config file\n");
		return 1;
	}
	mjv_gui_main(argc, argv, config);

	mjv_config_destroy(&config);

	return 0;
}
