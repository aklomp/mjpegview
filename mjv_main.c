#include <glib.h>
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_gui.h"

int
main (int argc, char **argv)
{
	GList *sources = NULL;

	struct mjv_source *dannycam;
	struct mjv_source *lunchcam;

	if ((dannycam = mjv_source_create_from_network("192.168.2.125", 80, "/video.mjpg", "admin", "admin", mjv_gui_show_frame)) == NULL) {
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
	sources = g_list_append(sources, lunchcam);

	mjv_gui_main(argc, argv, sources);

	mjv_source_destroy(lunchcam);
	mjv_source_destroy(dannycam);

	return 0;
}
