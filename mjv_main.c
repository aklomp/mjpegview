#include <stdbool.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "mjv_config.h"
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_gui.h"

#define MJV_SOURCE(x)  ((struct mjv_source *)(x))

static const char config_file[] = "config";

int
main (int argc, char **argv)
{
	GList *sources = NULL;

	struct mjv_config *config;

	struct mjv_source *dannycam;
	struct mjv_source *servercam;
	struct mjv_source *lunchcam;

	if ((config = mjv_config_init()) == NULL) {
		g_printerr("Fatal: could not load config\n");
		return 0;
	}
	if (mjv_config_read_file(config, config_file) == 0) {
		// FIXME app should work without a config file
		g_printerr("Fatal: could not read config\n");
		return 0;
	}
	if ((dannycam = mjv_source_create_from_network("192.168.2.125", 80, "/video.mjpg", "admin", "admin", mjv_gui_show_frame)) == NULL) {
		return 1;
	}
	if ((servercam = mjv_source_create_from_network("192.168.2.67", 80, "/MJPEG.CGI", "", "", mjv_gui_show_frame)) == NULL) {
		return 1;
	}
	if ((lunchcam = mjv_source_create_from_network("192.168.2.79", 80, "/nphMotionJpeg?Resolution=640x480&Quality=Standard", "administrator", "administrator1", mjv_gui_show_frame)) == NULL) {
		return 1;
	}
	if (mjv_source_set_name(dannycam, "DannyCam") == 0) {
		g_printerr("Could not set name on DannyCam");
	}
	if (mjv_source_set_name(lunchcam, "LunchCam") == 0) {
		g_printerr("Could not set name on LunchCam");
	}
	sources = g_list_append(sources, dannycam);
	sources = g_list_append(sources, servercam);
	sources = g_list_append(sources, lunchcam);

	mjv_gui_main(argc, argv, sources);

	g_list_free_full(sources, (GDestroyNotify)mjv_source_destroy);

	mjv_config_destroy(config);

	return 0;
}
