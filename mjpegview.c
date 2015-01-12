#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "mjv_log.h"
#include "mjv_config.h"
#include "mjv_gui.h"

static void
process_cmdline (int argc, char **argv, char **filename)
{
	int c, option_index = 0;
	static struct option long_options[] = {
		{ "debug", 0, 0, 'd' },
		{ "filename", 1, 0, 'f' },
		{ 0, 0, 0, 0 }
	};
	for (;;) {
		if ((c = getopt_long(argc, argv, "df:", long_options, &option_index)) == -1) {
			break;
		}
		switch (c)
		{
			case 'd': log_debug_on(); break;
			case 'f': *filename = strdup(optarg); break;
		}
	}
}

int
main (int argc, char **argv)
{
	char *filename = NULL;
	struct mjv_config *config;

	process_cmdline(argc, argv, &filename);

	if (filename == NULL) {
		filename = strdup("config");
	}
	if ((config = mjv_config_create_from_file(filename)) == NULL) {
		log_error("Fatal: could not create config from file\n");
		free(filename);
		return 1;
	}
	mjv_gui_main(argc, argv, config);

	mjv_config_destroy(&config);
	free(filename);

	return 0;
}
