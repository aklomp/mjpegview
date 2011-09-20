#include <stdbool.h>
#include <pthread.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "mjv_frame.h"
#include "mjv_source.h"

struct mjv_thread {
	cairo_t   *cairo;
	GMutex    *mutex;
	GdkPixbuf *pixbuf;
	GtkWidget *canvas;
	pthread_t *pthread;
	unsigned int width;
	unsigned int height;
	struct mjv_source *source;
};

static void *thread_main (void *);
static void callback_got_frame (struct mjv_frame *, void *);

static void
print_source_name (cairo_t *cr, const char *name)
{
	cairo_set_font_size(cr, 12);
	cairo_move_to(cr, 10, 15);

	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
	cairo_set_line_width(cr, 3);
	cairo_text_path(cr, name);
	cairo_stroke(cr);
	cairo_fill(cr);

	cairo_move_to(cr, 10, 15);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_text_path(cr, name);
	cairo_fill(cr);
}

static gboolean
canvas_repaint (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	(void)event;
	const char *source_name;
	struct mjv_thread *t = (struct mjv_thread *)(user_data);

	// TODO: constant creating/destroying of the cairo context
	// seems wasteful:
	g_mutex_lock(t->mutex);

	t->cairo = gdk_cairo_create(widget->window);

	if (t->pixbuf == NULL) {
		cairo_set_source_rgb(t->cairo, 0.5, 0.5, 0.5);
	}
	else {
		gdk_cairo_set_source_pixbuf(t->cairo, t->pixbuf, 0, 0);
	}
	cairo_paint(t->cairo);

	if ((source_name = mjv_source_get_name(t->source)) != NULL) {
		print_source_name(t->cairo, source_name);
	}
	cairo_destroy(t->cairo);
	g_mutex_unlock(t->mutex);
	return TRUE;
}

struct mjv_thread *
mjv_thread_create (struct mjv_source *source)
{
	// This function creates a thread object, but does not run it.
	// To run, call mjv_thread_run on the thread object.

	struct mjv_thread *t = g_malloc(sizeof(*t));

	t->width   = 320;
	t->height  = 240;
	t->cairo   = NULL;
	t->pixbuf  = NULL;
	t->source  = source;
	t->mutex   = g_mutex_new();
	t->canvas  = gtk_drawing_area_new();
	t->pthread = g_malloc(sizeof(pthread_t));

	gtk_widget_set_size_request(t->canvas, t->width, t->height);
	gtk_signal_connect(GTK_OBJECT(t->canvas), "expose_event", GTK_SIGNAL_FUNC(canvas_repaint), t);

	return t;
}

void
mjv_thread_destroy (struct mjv_thread *t)
{
	g_assert(t != NULL);
	g_mutex_free(t->mutex);
	if (t->pixbuf != NULL) {
		g_object_unref(t->pixbuf);
	}
	g_free(t->pthread);
	g_free(t);
}

bool
mjv_thread_run (struct mjv_thread *t)
{
	GError *error = NULL;

	g_assert(t != NULL);
	if (pthread_create(t->pthread, NULL, thread_main, t) != 0) {
		g_printerr("%s\n", error->message);
		return false;
	}
	return true;
}

struct mjv_thread *
mjv_thread_term (struct mjv_thread *t)
{
	// Terminate a thread by sending a sigterm, then waiting for it to join
	g_assert(t != NULL);
	return NULL;
}

unsigned int
mjv_thread_get_height (struct mjv_thread *t)
{
	g_assert(t != NULL);
	return t->height;
}

unsigned int
mjv_thread_get_width (struct mjv_thread *t)
{
	g_assert(t != NULL);
	return t->width;
}

const GtkWidget *
mjv_thread_get_canvas (struct mjv_thread *t)
{
	g_assert(t != NULL);
	return t->canvas;
}

static void *
thread_main (void *user_data)
{
	struct mjv_thread *t = (struct mjv_thread *)user_data;

	mjv_source_set_callback(t->source, &callback_got_frame, (void *)t);
	mjv_source_capture(t->source);
	pthread_exit(NULL);
	return NULL;
}

static void
destroy_pixels (guchar *pixels, gpointer data)
{
	(void)data;
	g_free(pixels);
}

static void
widget_force_size (GtkWidget *widget, unsigned int height, unsigned int width)
{
	// This function is not thread safe; caller must call g_thread_enter()
	if (height != (unsigned int)widget->allocation.height || width != (unsigned int)widget->allocation.width) {
		gtk_widget_set_size_request(widget, width, height);
	}
}

static void
callback_got_frame (struct mjv_frame *frame, void *user_data)
{
	unsigned char *pixels;
	struct mjv_thread *thread = (struct mjv_thread *)(user_data);

	g_assert(frame != NULL);
	g_assert(thread != NULL);

	// Convert from JPEG to pixbuf:
	if ((pixels = mjv_frame_to_pixbuf(frame)) == NULL) {
		mjv_frame_destroy(frame);
		return;
	}
	unsigned int width = mjv_frame_get_width(frame);
	unsigned int height = mjv_frame_get_height(frame);
	unsigned int row_stride = mjv_frame_get_row_stride(frame);

	g_mutex_lock(thread->mutex);

	g_assert(width > 0);
	g_assert(height > 0);
	g_assert(row_stride > 0);

	thread->width  = width;
	thread->height = height;
	widget_force_size(thread->canvas, height, width);

	// Replace existing pixbuf:
	if (thread->pixbuf != NULL) {
		g_object_unref(thread->pixbuf);
	}
	thread->pixbuf = gdk_pixbuf_new_from_data(
		pixels,			// data
		GDK_COLORSPACE_RGB,	// colorspace
		FALSE,			// has_alpha
		8,			// bits_per_sample
		width,			// width
		height,			// height
		row_stride,		// row stride in bytes
		destroy_pixels,		// destroy function for pixel data
		NULL			// user argument to the above
	);
	g_mutex_unlock(thread->mutex);

	// FIXME use bit depth from frame, etc:
	gdk_threads_enter();
	gtk_widget_queue_draw(thread->canvas);
	gdk_threads_leave();

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);
	return;
}
