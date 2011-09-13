#include <stdlib.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "mjv_frame.h"
#include "mjv_source.h"

// Make this global because mjv_gui_show_frame needs it
// Make a proper getter for this once we're done.
GtkWidget *darea_one;
GtkWidget *darea_two;

unsigned int darea_width = 640;
unsigned int darea_height = 480;

static void
on_destroy (void)
{
	gtk_main_quit();
}

int
mjv_gui_main (int argc, char **argv, struct mjv_source *source_one, struct mjv_source *source_two)
{
	GError *error = NULL;

	gdk_threads_init();
	gtk_init(&argc, &argv);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "mjpegview");

	darea_one = gtk_drawing_area_new();
	gtk_widget_set_size_request(darea_one, darea_width, darea_height);

	darea_two = gtk_drawing_area_new();
	gtk_widget_set_size_request(darea_two, darea_width, darea_height);

	GtkWidget *box = gtk_hbox_new(FALSE, 0);

	gtk_container_add(GTK_CONTAINER(box), darea_one);
	gtk_container_add(GTK_CONTAINER(box), darea_two);
	gtk_container_add(GTK_CONTAINER(win), box);

	gtk_signal_connect(GTK_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);

	gtk_widget_show_all(win);

	// Create camera thread:
	if (!g_thread_create((GThreadFunc)mjv_source_capture, source_one, FALSE, &error)) {
		g_printerr("%s\n", error->message);
		return 1;
	}
	if (!g_thread_create((GThreadFunc)mjv_source_capture, source_two, FALSE, &error)) {
		g_printerr("%s\n", error->message);
		return 1;
	}
	// Enter main loop:
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();
	return 0;
}

void
mjv_gui_show_frame (struct mjv_source *s, struct mjv_frame *frame)
{
	unsigned char *pixmap;
	unsigned int frame_height;
	unsigned int frame_width;
	GtkWidget **darea;

	// Do the conversion from JPEG to pixmap:
	if ((pixmap = mjv_frame_to_pixmap(frame)) == NULL) {
		mjv_frame_destroy(frame);
		return;
	}
	// Get height and width of this frame:
	frame_height = mjv_frame_get_height(frame);
	frame_width = mjv_frame_get_width(frame);

	assert(frame_width > 0);
	assert(frame_height > 0);

	// Get correct drawing area for this source:
	darea = (mjv_source_get_id(s) == 1) ? &darea_one : &darea_two;

	// Adjust size of darea to frame if different from previous:
	if (frame_height != darea_height || frame_width != darea_width) {
		gtk_widget_set_size_request(*darea, frame_width, frame_height);
		darea_height = frame_height;
		darea_width = frame_width;
	}
	// FIXME use bit depth from frame, etc:
	gdk_threads_enter();
	gdk_draw_rgb_image((*darea)->window, (*darea)->style->fg_gc[GTK_STATE_NORMAL],
		      0, 0,
		      frame_width,
		      frame_height,
		      GDK_RGB_DITHER_MAX, pixmap, frame_width * 3);

	gdk_threads_leave();
	g_free(pixmap);

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);
	return;
}
