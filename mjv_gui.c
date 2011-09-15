#include <stdlib.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "mjv_frame.h"
#include "mjv_source.h"

static gboolean darea_expose (GtkWidget *, GdkEventExpose *, gpointer);

GList *darea_list = NULL;

// Cast a GList's data pointer to an object pointer:
#define MJV_DAREA(x)	((struct mjv_darea *)((x)->data))
#define MJV_SOURCE(x)	((struct mjv_source *)((x)->data))

struct mjv_darea {
	guint width;
	guint height;
	cairo_t *cairo;
	GdkPixbuf *pixbuf;
	GtkWidget *drawing_area;
	GMutex *pixbuf_mutex;
	struct mjv_source *source;
};

static struct mjv_darea *mjv_darea_create (struct mjv_source *);

static struct mjv_darea *
mjv_darea_create (struct mjv_source *source)
{
	struct mjv_darea *d = g_malloc(sizeof(*d));

	d->width = 320;
	d->height = 240;
	d->cairo = NULL;
	d->pixbuf = NULL;
	d->pixbuf_mutex = g_mutex_new();
	d->drawing_area = gtk_drawing_area_new();
	d->source = source;
	gtk_widget_set_size_request(d->drawing_area, d->width, d->height);

	return d;
}

#if 0
static void
mjv_darea_destroy (struct mjv_darea *d)
{
	g_mutex_free(d->pixbuf_mutex);
	g_object_unref(d->pixbuf);
	g_free(d);
}
#endif

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
		darea_list = g_list_append(darea_list, mjv_darea_create(MJV_SOURCE(link)));
	}
	GtkWidget *box = gtk_hbox_new(FALSE, 0);

	// Add each darea to the box:
	for (link = g_list_first(darea_list); link; link = g_list_next(link)) {
		gtk_container_add(GTK_CONTAINER(box), MJV_DAREA(link)->drawing_area);
		gtk_signal_connect(GTK_OBJECT(MJV_DAREA(link)->drawing_area), "expose_event", GTK_SIGNAL_FUNC(darea_expose), MJV_DAREA(link));
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


static gboolean
darea_expose (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	const char *source_name;
	(void)event;
	struct mjv_darea *d = (struct mjv_darea *)(user_data);

	// TODO: constant creating/destroying of the cairo context
	// seems wasteful:
	d->cairo = gdk_cairo_create(widget->window);
	if (d->pixbuf == NULL) {
		cairo_set_source_rgb(d->cairo, 0.5, 0.5, 0.5);
	}
	else {
		g_mutex_lock(d->pixbuf_mutex);
		gdk_cairo_set_source_pixbuf(d->cairo, d->pixbuf, 0, 0);
		g_mutex_unlock(d->pixbuf_mutex);
	}
	cairo_paint(d->cairo);

	// Source name
	if ((source_name = mjv_source_get_name(d->source)) == NULL) {
		cairo_set_font_size(d->cairo, 12);

		cairo_move_to(d->cairo, 10, 15);
		cairo_set_source_rgba(d->cairo, 0.0, 0.0, 0.0, 0.3);
		cairo_set_line_width(d->cairo, 3);
		cairo_text_path(d->cairo, source_name);
		cairo_stroke(d->cairo);
		cairo_fill(d->cairo);

		cairo_move_to(d->cairo, 10, 15);
		cairo_set_source_rgba(d->cairo, 1.0, 1.0, 1.0, 1.0);
		cairo_text_path(d->cairo, source_name);
		cairo_fill(d->cairo);
	}

	cairo_destroy(d->cairo);

//	gdk_draw_rgb_image(widget->window, widget->style->fg_gc[GTK_STATE_NORMAL],
//		      0, 0, IMAGE_WIDTH, IMAGE_HEIGHT,
//		      GDK_RGB_DITHER_MAX, rgbbuf, IMAGE_WIDTH * 3);
//
	return TRUE;
}

static void
destroy_pixels (guchar *pixels, void *data)
{
	(void)data;
	g_free(pixels);
}

void
mjv_gui_show_frame (struct mjv_source *s, struct mjv_frame *frame)
{
	guchar *pixels;
	GList *link = NULL;

	g_assert(s != NULL);
	g_assert(frame != NULL);

	// Do the conversion from JPEG to pixbuf:
	if ((pixels = mjv_frame_to_pixbuf(frame)) == NULL) {
		mjv_frame_destroy(frame);
		return;
	}
	// Get height and width of this frame:
	guint width = mjv_frame_get_width(frame);
	guint height = mjv_frame_get_height(frame);

	assert(width  > 0);
	assert(height > 0);

	// Get correct drawing area for this source:
	// TODO: looping over a list is not the fastest thing.
	for (link = g_list_first(darea_list); link; link = g_list_next(link)) {
		if (s == MJV_DAREA(link)->source) {
			break;
		}
	}
	// Assert that we found a darea:
	g_assert(link != NULL);

	// Adjust size of darea to frame if different from previous:
	if (height != MJV_DAREA(link)->height || width != MJV_DAREA(link)->width) {
		gtk_widget_set_size_request(MJV_DAREA(link)->drawing_area, width, height);
		MJV_DAREA(link)->width = width;
		MJV_DAREA(link)->height = height;
	}
//	pixmap_to_disabled( pixmap, height * width );

	// Lock pixbuf, so that we don't enter a race condition with the
	// expose-event handler
	g_mutex_lock(MJV_DAREA(link)->pixbuf_mutex);

	// Remove reference to existing pixmap if exists:
	if (MJV_DAREA(link)->pixbuf != NULL) {
		g_object_unref(MJV_DAREA(link)->pixbuf);
	}
	// Load our pixbuf data into this mjv_darea's GdkPixbuf member:
	MJV_DAREA(link)->pixbuf = gdk_pixbuf_new_from_data
		( pixels		// data
		, GDK_COLORSPACE_RGB	// colorspace
		, FALSE			// has_alpha
		, 8			// bits_per_sample
		, width			// width
		, height		// height
		, width * 3		// row stride in bytes
		, destroy_pixels	// destroy function for pixel data
		, NULL			// user argument to the above
		) ;
	g_mutex_unlock(MJV_DAREA(link)->pixbuf_mutex);

	// FIXME use bit depth from frame, etc:
	gdk_threads_enter();
	gdk_window_invalidate_rect(MJV_DAREA(link)->drawing_area->window, NULL, FALSE);
	gdk_threads_leave();

//	gdk_draw_rgb_image(MJV_DAREA(link)->drawing_area->window, MJV_DAREA(link)->drawing_area->style->fg_gc[GTK_STATE_NORMAL],
//		      0, 0,
//		      width,
//		      height,
//		      GDK_RGB_DITHER_MAX, pixels, width * 3);

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);
	return;
}

#if 0
static void
pixmap_to_greyscale (char *pixmap, guint num_triplets)
{
	guint grey;
	char *r = pixmap + 0;
	char *g = pixmap + 1;
	char *b = pixmap + 2;
	char *end = pixmap + num_triplets * 3;

	// Copied from ppmtopgm:
	// .299 r + .587 g + .114 b = 76/256 r + 150/256 g + 29/256 b = ( 76 r + 150 g + 29 b ) / 256
	// Max divisor is 255/256, which becomes 255 when right-shifted.
	while (r < end) {
		grey = (((guint)(*r)) * 76
		      + ((guint)(*g)) * 150
		      + ((guint)(*b)) * 29) / 256;

		*r = *g = *b = (char)grey;
		r += 3;
		g += 3;
		b += 3;
	}
}

static void
pixmap_to_disabled (char *pixmap, guint num_triplets)
{
	guint grey;
	char *r = pixmap + 0;
	char *g = pixmap + 1;
	char *b = pixmap + 2;
	char *end = pixmap + num_triplets * 3;

	// Same formula as above, but with a different divisor:
	// Range: 25..(255 / 350 * 255 + 25) = 25..210
	while (r < end) {
		grey = (((guint)(*r) & 0xff) * 76
		      + ((guint)(*g) & 0xff) * 150
		      + ((guint)(*b) & 0xff) * 29) / 350 + 25;

		*r = *g = *b = (char)grey;
		r += 3;
		g += 3;
		b += 3;
	}
}
#endif
