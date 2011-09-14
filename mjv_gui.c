#include <stdlib.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "mjv_frame.h"
#include "mjv_source.h"

GList *darea_list = NULL;

struct darea {
	guint width;
	guint height;
	guint source_id;
	GtkWidget *darea;
};

static struct darea *darea_create (struct mjv_source *);

static struct darea *
darea_create (struct mjv_source *source)
{
	struct darea *d = g_malloc(sizeof(*d));

	d->width = 640;
	d->height = 480;
	d->darea = gtk_drawing_area_new();
	d->source_id = mjv_source_get_id(source);
	gtk_widget_set_size_request(d->darea, d->width, d->height);
	return d;
}

static void
on_destroy (void)
{
	gtk_main_quit();
}

// TODO GList is overkill, use GArray or GSList something
int
mjv_gui_main (int argc, char **argv, GList *sources)
{
	GError *error = NULL;
	GList *link = NULL;

	gdk_threads_init();
	gtk_init(&argc, &argv);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "mjpegview");

	// Create a darea object for each source:
	for (link = g_list_first(sources); link; link = g_list_next(link)) {
		darea_list = g_list_append(darea_list, darea_create((struct mjv_source *)link->data));
	}
	GtkWidget *box = gtk_hbox_new(FALSE, 0);

	// Add each darea to the box:
	for (link = g_list_first(darea_list); link; link = g_list_next(link)) {
		gtk_container_add(GTK_CONTAINER(box), ((struct darea *)link->data)->darea);
	}
	gtk_container_add(GTK_CONTAINER(win), box);

	gtk_signal_connect(GTK_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);

	gtk_widget_show_all(win);

	// Create camera threads:
	for (link = g_list_first(sources); link; link = g_list_next(link)) {
		if (!g_thread_create((GThreadFunc)mjv_source_capture, link->data, FALSE, &error)) {
			g_printerr("%s\n", error->message);
			return 1;
		}
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
	GList *link = NULL;
	struct darea *darea;

	g_assert(s != NULL);
	g_assert(frame != NULL);

	guint source_id = mjv_source_get_id(s);

	// Do the conversion from JPEG to pixmap:
	if ((pixmap = mjv_frame_to_pixmap(frame)) == NULL) {
		mjv_frame_destroy(frame);
		return;
	}
	// Get height and width of this frame:
	guint height = mjv_frame_get_height(frame);
	guint width = mjv_frame_get_width(frame);

	assert(width  > 0);
	assert(height > 0);

	// Get correct drawing area for this source:
	// TODO: looping over a list is not the fastest thing.
	for (link = g_list_first(darea_list); link; link = g_list_next(link)) {
		if (source_id == ((struct darea *)link->data)->source_id) {
			break;
		}
	}
	// Assert that we found a darea:
	g_assert(link != NULL);
	// TODO do not name the struct and the drawable both 'darea', it's confusing as hell
	darea = (struct darea *)link->data;

	// Adjust size of darea to frame if different from previous:
	if (height != darea->height || width != darea->width) {
		g_printerr("Size request\n");
		gtk_widget_set_size_request(darea->darea, width, height);
		darea->height = height;
		darea->width = width;
	}
	// FIXME use bit depth from frame, etc:
	gdk_threads_enter();
	gdk_draw_rgb_image(darea->darea->window, darea->darea->style->fg_gc[GTK_STATE_NORMAL],
		      0, 0,
		      width,
		      height,
		      GDK_RGB_DITHER_MAX, pixmap, width * 3);

	gdk_threads_leave();
	g_free(pixmap);

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);
	return;
}
