#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <glib.h>
#include <time.h>
#include <gtk/gtk.h>

#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_thread.h"

// This is a private structure that describes the
// spinner element (the "on-hold" spinning circle)
struct spinner {
	unsigned int step;
	unsigned int active;
	unsigned int iterations;
	struct timespec start;

	pthread_t pthread;
};

struct mjv_thread {
	cairo_t   *cairo;
	GMutex    *mutex;
	GdkPixbuf *pixbuf;
	GtkWidget *canvas;
	unsigned int width;
	unsigned int height;
	struct spinner spinner;
	struct mjv_source *source;

	pthread_t      pthread;
	pthread_attr_t pthread_attr;
};

#define SPINNER_STEPS	12

static void *thread_main (void *);
static void callback_got_frame (struct mjv_frame *, void *);
static void draw_spinner (cairo_t *, int, int, int);
static void *spinner_thread_main (void *);

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

	// Note: this function is EXCLUSIVELY called from gtk_main() as a callback,
	// and thus within the global thread lock. Hence NO locking is necessary here
	// (since this function is inherently single-threaded). Note that NONE of
	// the functions that this function calls, can use gdk_threads_enter() /
	// leave(), because doing so results in deadlock (the global thread lock is
	// not reentrant),

	// TODO: constant creating/destroying of the cairo context
	// seems wasteful:

	// This mutex ensures that no running thread can update the mjv_thread data
	// object from the frame callback while we are here:
	g_mutex_lock(t->mutex);
	t->cairo = gdk_cairo_create(widget->window);

	if (t->pixbuf == NULL) {
		cairo_set_source_rgb(t->cairo, 0.1, 0.1, 0.1);
	}
	else {
		gdk_cairo_set_source_pixbuf(t->cairo, t->pixbuf, 0, 0);
	}
	cairo_paint(t->cairo);

	if ((source_name = mjv_source_get_name(t->source)) != NULL) {
		print_source_name(t->cairo, source_name);
	}
	if (t->spinner.active == 1) {
		draw_spinner(t->cairo, widget->allocation.width / 2, widget->allocation.height / 2, t->spinner.step);
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

	t->width   = 640;
	t->height  = 480;
	t->cairo   = NULL;
	t->pixbuf  = NULL;
	t->source  = source;
	t->mutex   = g_mutex_new();
	t->canvas  = gtk_drawing_area_new();

	t->spinner.step = 0;
	t->spinner.active = 0;

	pthread_attr_init(&t->pthread_attr);
	pthread_attr_setdetachstate(&t->pthread_attr, PTHREAD_CREATE_JOINABLE);

	gtk_widget_set_size_request(t->canvas, t->width, t->height);
	gtk_signal_connect(GTK_OBJECT(t->canvas), "expose_event", GTK_SIGNAL_FUNC(canvas_repaint), t);

	return t;
}

void
mjv_thread_destroy (struct mjv_thread *t)
{
	g_assert(t != NULL);

	if (t->spinner.active) {
		mjv_thread_hide_spinner(t);
	}
	g_mutex_free(t->mutex);
	pthread_attr_destroy(&t->pthread_attr);
	if (t->pixbuf != NULL) {
		g_object_unref(t->pixbuf);
	}
	g_free(t);
}

bool
mjv_thread_run (struct mjv_thread *t)
{
	if (pthread_create(&t->pthread, &t->pthread_attr, thread_main, t) != 0) {
		return false;
	}
	return true;
}

bool
mjv_thread_cancel (struct mjv_thread *t)
{
	// Terminate a thread by sending it a cancel request, then waiting for
	// it to join. The cancel request will be caught in mjv_source_capture.
	if (pthread_cancel(t->pthread) != 0) {
		return false;
	}
	if (pthread_join(t->pthread, NULL) != 0) {
		return false;
	}
	return true;
}

void
mjv_thread_show_spinner (struct mjv_thread *t)
{
	g_assert(t != NULL);
	g_assert(t->spinner.active == 0);

	// This function spawns a new pthread that wakes every x milliseconds
	// and requests a redraw of the frame area.

	t->spinner.iterations = 0;
	clock_gettime(CLOCK_REALTIME, &t->spinner.start);
	if (pthread_create(&t->spinner.pthread, NULL, spinner_thread_main, t) != 0) {
		return;
	}
	t->spinner.active = 1;
}

void
mjv_thread_hide_spinner (struct mjv_thread *t)
{
	g_assert(t != NULL);
	g_assert(t->spinner.active == 1);

	if (pthread_cancel(t->spinner.pthread) != 0) {
		return;
	}
	t->spinner.active = 0;
	gtk_widget_queue_draw( t->canvas );
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

	// The thread responds to pthread_cancel requests, but only
	// at cancellation points. We set up mjv_source_capture's pselect()
	// statement to be the only cancellation point that qualifies.
	// The thread will then only cancel when waiting for IO.
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	mjv_source_set_callback(t->source, &callback_got_frame, (void *)t);
	mjv_thread_show_spinner(t);
	if (mjv_source_open(t->source) == 0) {
		mjv_thread_hide_spinner(t);
		return NULL;
	}
	mjv_thread_hide_spinner(t);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	mjv_source_capture(t->source);
	return NULL;
}

static void
destroy_pixels (guchar *pixels, gpointer data)
{
	(void)data;
	g_free(pixels);
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

	gdk_threads_enter();
	g_mutex_lock(thread->mutex);

	g_assert(width > 0);
	g_assert(height > 0);
	g_assert(row_stride > 0);

	thread->width  = width;
	thread->height = height;

	if (height != (unsigned int)thread->canvas->allocation.height
	 || width != (unsigned int)thread->canvas->allocation.width) {
		gtk_widget_set_size_request(thread->canvas, width, height);
	}
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
	gtk_widget_queue_draw(thread->canvas);

	g_mutex_unlock(thread->mutex);
	gdk_threads_leave();

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);

	return;
}

static void
draw_spinner (cairo_t *cr, int x, int y, int step)
{
	static float rsmall = 3.0;
	static float sin[] = { 0.0, 9.0, 18.0 * 0.866, 18.0 };	// r = 18

#define TWO_PI        6.28318530717958647692
#define STEP_RGBA(n)  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.9 - 0.06 * ((n + step) % SPINNER_STEPS))
#define STEP_PAINT    cairo_close_path(cr); cairo_fill(cr)

	STEP_RGBA( 0); cairo_arc(cr, x + sin[0], y + sin[3], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 1); cairo_arc(cr, x + sin[1], y + sin[2], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 2); cairo_arc(cr, x + sin[2], y + sin[1], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 3); cairo_arc(cr, x + sin[3], y + sin[0], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 4); cairo_arc(cr, x + sin[2], y - sin[1], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 5); cairo_arc(cr, x + sin[1], y - sin[2], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 6); cairo_arc(cr, x + sin[0], y - sin[3], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 7); cairo_arc(cr, x - sin[1], y - sin[2], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 8); cairo_arc(cr, x - sin[2], y - sin[1], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 9); cairo_arc(cr, x - sin[3], y - sin[0], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA(10); cairo_arc(cr, x - sin[2], y + sin[1], rsmall, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA(11); cairo_arc(cr, x - sin[1], y + sin[2], rsmall, 0, TWO_PI); STEP_PAINT;

#undef STEP_PAINT
#undef STEP_RGBA
#undef TWO_PI
}

static void *
spinner_thread_main (void *user_data)
{
	struct timespec now;
	struct timespec wake;
	struct mjv_thread *t = (struct mjv_thread *)user_data;

#define ITERS_PER_SEC  (SPINNER_STEPS)
#define INTERVAL_NSEC  (1000000000 / ITERS_PER_SEC)

	// This function runs the spinner thread.
	// Every interval, update spinner step and request
	// a redraw of the canvas. Continue till canceled.
	for (;;)
	{
		// Get absolute time, calculated from the start time and the number
		// of iterations, of when the next tick should be issued. Aligning
		// the timing to an absolute clock prevents framerate drift.
		wake.tv_sec  = t->spinner.start.tv_sec + t->spinner.iterations / ITERS_PER_SEC;
		wake.tv_nsec = t->spinner.start.tv_nsec + (t->spinner.iterations % ITERS_PER_SEC) * INTERVAL_NSEC;
		if (wake.tv_nsec >= 1000000000) {
			wake.tv_sec++;
			wake.tv_nsec -= 1000000000;
		}
		// `wake' holds the time that we should wake up at. If this time is
		// already in the past, we are out of sync and rebase time to `now':
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > wake.tv_sec || (now.tv_sec == wake.tv_sec && now.tv_nsec >= wake.tv_nsec)) {
			t->spinner.iterations = 0;
			memcpy(&t->spinner.start, &now, sizeof(struct timespec));
		}
		else {
			while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wake, NULL) != 0);
		}
		t->spinner.step = (t->spinner.step + 1) % SPINNER_STEPS;
		gdk_threads_enter();
		gtk_widget_queue_draw(t->canvas);
		gdk_threads_leave();
		t->spinner.iterations++;
	}
	return NULL;

#undef INTERVAL_USEC
#undef ITERS_PER_SEC
}
