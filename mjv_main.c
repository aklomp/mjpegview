#include <glib.h>
#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_gui.h"

int
main (int argc, char **argv)
{
	struct mjv_source *dannycam;
	struct mjv_source *lunchcam;

	if ((dannycam = mjv_source_create_from_network("192.168.2.125", 80, "/video.mjpg", "admin", "admin", mjv_gui_show_frame)) == NULL) {
		return 1;
	}
	if ((lunchcam = mjv_source_create_from_network("192.168.2.79", 80, "/nphMotionJpeg?Resolution=640x480&Quality=Standard", "administrator", "administrator1", mjv_gui_show_frame)) == NULL) {
		return 1;
	}
	mjv_gui_main(argc, argv, lunchcam, dannycam);

	mjv_source_destroy(lunchcam);
	mjv_source_destroy(dannycam);

	return 0;
}
