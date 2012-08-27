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

enum state
{ STATE_DISCONNECTED
, STATE_CONNECTING
, STATE_CONNECTED
};

// This is a private structure that describes the
// spinner element (the "on-hold" spinning circle)
struct spinner {
	GMutex *mutex;
	unsigned int step;
	unsigned int iterations;
	struct timespec start;

	pthread_t pthread;
};

#define FRAMERATE_MEMORY  15

struct framerate {
	float fps;
	int nmemory;
	struct timespec memory[FRAMERATE_MEMORY];
	GMutex *mutex;
	pthread_t pthread;
};

struct toolbar {
	GtkWidget *toolbar;
	GtkToolItem *btn_record;
	GtkToolItem *btn_connect;
};

struct statusbar {
	GtkWidget *lbl_status;
	GtkWidget *lbl_fps;
	GtkWidget *lbl_framebuf;
};

struct mjv_thread {
	cairo_t   *cairo;
	GMutex    *mutex;
	GdkPixbuf *pixbuf;
	GtkWidget *frame;
	GtkWidget *canvas;
	unsigned int width;
	unsigned int height;
	unsigned int blinker;
	struct spinner spinner;
	struct mjv_source *source;
	struct framerate framerate;
	struct toolbar toolbar;
	struct statusbar statusbar;
	enum state state;

	pthread_t      pthread;
	pthread_attr_t pthread_attr;
};

#define BLINKER_ALPHA	0.3
#define BLINKER_HEIGHT	8
#define SPINNER_STEPS	12

#define g_malloc_fail(s)   ((s = g_try_malloc(sizeof(*(s)))) == NULL)

static void *thread_main (void *);
static void callback_got_frame (struct mjv_frame *, void *);
static void draw_blinker (cairo_t *, int, int, int);
static void draw_spinner (cairo_t *, int, int, int);
static void *spinner_thread_main (void *);
static void framerate_insert_datapoint (struct mjv_thread *, const struct timespec *const);
static void framerate_estimator (struct mjv_thread *);
static float timespec_diff (struct timespec *, struct timespec *);

static void framerate_thread_run(struct mjv_thread *t);
static void framerate_thread_kill(struct mjv_thread *t);
static void *framerate_thread_main(void *user_data);

static void create_frame(struct mjv_thread *thread);
static void create_frame_toolbar(struct mjv_thread *thread);
static GtkWidget *create_frame_statusbar(struct mjv_thread *thread);

static void create_frame(struct mjv_thread *thread);
static void create_frame_toolbar(struct mjv_thread *thread);
static GtkWidget *create_frame_statusbar(struct mjv_thread *thread);

static char *status_string(struct mjv_thread *thread);
static void update_state(struct mjv_thread *thread, enum state state);

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
	if (t->state == STATE_CONNECTING) {
		draw_spinner(t->cairo, widget->allocation.width / 2, widget->allocation.height / 2, t->spinner.step);
	}
	else if (t->state == STATE_CONNECTED) {
		draw_blinker(t->cairo, 4, t->height - 4 - BLINKER_HEIGHT, t->blinker);
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

	struct mjv_thread *t;

	if (g_malloc_fail(t)) {
		return NULL;
	}
	memset(t, 0, sizeof(*t));

	t->width   = 640;
	t->height  = 480;
	t->source  = source;
	t->mutex   = g_mutex_new();
	t->canvas  = gtk_drawing_area_new();
	t->state   = STATE_DISCONNECTED;

	t->spinner.step = 0;
	t->spinner.mutex = g_mutex_new();

	t->framerate.fps = -1.0;
	t->framerate.mutex = g_mutex_new();

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

	if (t->state == STATE_CONNECTING) {
		mjv_thread_hide_spinner(t);
	}
	g_mutex_free(t->mutex);
	g_mutex_free(t->spinner.mutex);
	g_mutex_free(t->framerate.mutex);
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

GtkWidget *
mjv_thread_create_area(struct mjv_thread *t)
{
	// Create the frame for this thread.
	create_frame(t);
	return t->frame;
}

void
mjv_thread_show_spinner (struct mjv_thread *t)
{
	g_assert(t != NULL);

	// This function spawns a new pthread that wakes every x milliseconds
	// and requests a redraw of the frame area.

	g_mutex_lock(t->spinner.mutex);
	t->spinner.iterations = 0;
	clock_gettime(CLOCK_REALTIME, &t->spinner.start);
	pthread_create(&t->spinner.pthread, NULL, spinner_thread_main, t);
	g_mutex_unlock(t->spinner.mutex);
}

void
mjv_thread_hide_spinner (struct mjv_thread *t)
{
	g_assert(t != NULL);

	if (pthread_cancel(t->spinner.pthread) != 0) {
		return;
	}
	gtk_widget_queue_draw(t->canvas);
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
	update_state(t, STATE_CONNECTING);
	mjv_thread_show_spinner(t);
	if (mjv_source_open(t->source) == 0) {
		mjv_thread_hide_spinner(t);
		update_state(t, STATE_DISCONNECTED);
		return NULL;
	}
	update_state(t, STATE_CONNECTED);
	mjv_thread_hide_spinner(t);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	framerate_thread_run(t);
	mjv_source_capture(t->source);
	update_state(t, STATE_DISCONNECTED);
	framerate_thread_kill(t);
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

	mjv_thread_hide_spinner(thread);

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
	thread->blinker = 1 - thread->blinker;
	gtk_widget_queue_draw(thread->canvas);

	framerate_insert_datapoint(thread, mjv_frame_get_timestamp(frame));

	g_mutex_unlock(thread->mutex);
	gdk_threads_leave();

	// Frame is no longer needed, and we took responsibility for it:
	mjv_frame_destroy(frame);

	return;
}

static void
draw_blinker (cairo_t *cr, int x, int y, int toggle)
{
	// The 'blinker' is the small checkerboard that toggles whenever a
	// frame is received. It indicates that a new frame has been received,
	// and that the capture is working.

#define RIB (BLINKER_HEIGHT / 2)

	double color_a = (toggle) ? 0.0 : 1.0;
	double color_b = (toggle) ? 1.0 : 0.0;

	cairo_set_source_rgba(cr, color_a, color_a, color_a, BLINKER_ALPHA);
	cairo_rectangle(cr, x, y, RIB, RIB);
	cairo_rectangle(cr, x + RIB, y + RIB, RIB, RIB);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, color_b, color_b, color_b, BLINKER_ALPHA);
	cairo_rectangle(cr, x + RIB, y, RIB, RIB);
	cairo_rectangle(cr, x, y + RIB, RIB, RIB);
	cairo_fill(cr);

#undef RIB
}

static void
draw_spinner (cairo_t *cr, int x, int y, int step)
{
#define TWO_PI        6.28318530717958647692
#define RLARGE        18.0
#define RSMALL        3.0
#define STEP_RGBA(n)  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.9 - 0.06 * ((n + step) % SPINNER_STEPS))
#define STEP_PAINT    cairo_close_path(cr); cairo_fill(cr)

	static float sin[] = { 0.0, RLARGE * 0.5, RLARGE * 0.86602540378443864676, RLARGE };

	STEP_RGBA( 0); cairo_arc(cr, x + sin[0], y + sin[3], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 1); cairo_arc(cr, x + sin[1], y + sin[2], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 2); cairo_arc(cr, x + sin[2], y + sin[1], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 3); cairo_arc(cr, x + sin[3], y + sin[0], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 4); cairo_arc(cr, x + sin[2], y - sin[1], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 5); cairo_arc(cr, x + sin[1], y - sin[2], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 6); cairo_arc(cr, x + sin[0], y - sin[3], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 7); cairo_arc(cr, x - sin[1], y - sin[2], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 8); cairo_arc(cr, x - sin[2], y - sin[1], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA( 9); cairo_arc(cr, x - sin[3], y - sin[0], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA(10); cairo_arc(cr, x - sin[2], y + sin[1], RSMALL, 0, TWO_PI); STEP_PAINT;
	STEP_RGBA(11); cairo_arc(cr, x - sin[1], y + sin[2], RSMALL, 0, TWO_PI); STEP_PAINT;

#undef STEP_PAINT
#undef STEP_RGBA
#undef RSMALL
#undef RLARGE
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
		g_mutex_lock( t->spinner.mutex );
		wake.tv_sec  = t->spinner.start.tv_sec + t->spinner.iterations / ITERS_PER_SEC;
		wake.tv_nsec = t->spinner.start.tv_nsec + (t->spinner.iterations % ITERS_PER_SEC) * INTERVAL_NSEC;
		g_mutex_unlock(t->spinner.mutex);
		if (wake.tv_nsec >= 1000000000) {
			wake.tv_sec++;
			wake.tv_nsec -= 1000000000;
		}
		// `wake' holds the time that we should wake up at. If this time is
		// already in the past, we are out of sync and rebase time to `now':
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > wake.tv_sec || (now.tv_sec == wake.tv_sec && now.tv_nsec >= wake.tv_nsec)) {
			g_mutex_lock(t->spinner.mutex);
			t->spinner.iterations = 0;
			memcpy(&t->spinner.start, &now, sizeof(struct timespec));
			g_mutex_unlock(t->spinner.mutex);
		}
		else {
			while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wake, NULL) != 0);
		}
		g_mutex_lock(t->spinner.mutex);
		t->spinner.step = (t->spinner.step + 1) % SPINNER_STEPS;
		t->spinner.iterations++;
		g_mutex_unlock(t->spinner.mutex);
		gdk_threads_enter();
		gtk_widget_queue_draw(t->canvas);
		gdk_threads_leave();
	}
	return NULL;

#undef INTERVAL_USEC
#undef ITERS_PER_SEC
}

static void
framerate_thread_run (struct mjv_thread *t)
{
	// TODO: error handling, ...
	pthread_create(&t->framerate.pthread, NULL, framerate_thread_main, t);
}

static void
framerate_thread_kill (struct mjv_thread *t)
{
	pthread_cancel(t->framerate.pthread);
}

static void *
framerate_thread_main (void *user_data)
{
	float fps;
	char buf[20];
	struct mjv_thread *t = (struct mjv_thread*)user_data;

	for (;;)
	{
		// Get FPS value:
		g_mutex_lock(t->framerate.mutex);
		framerate_estimator(t);
		fps = t->framerate.fps;
		g_mutex_unlock(t->framerate.mutex);

		// Create label:
		if (fps > 0.0) {
			snprintf(buf, 20, "%0.2f fps", fps);
			buf[19] = '\0';
		}
		else {
			strcpy(buf, "stalled");
		}
		// Change label:
		gdk_threads_enter();
		gtk_label_set_text(GTK_LABEL(t->statusbar.lbl_fps), buf);
		gtk_widget_queue_draw(t->statusbar.lbl_fps);
		gdk_threads_leave();

		sleep(1);
	}
	return NULL;
}

static void
framerate_insert_datapoint (struct mjv_thread *thread, const struct timespec *const ts)
{
	g_mutex_lock(thread->framerate.mutex);

	// Shift existing timestamps one over:
	memmove(&thread->framerate.memory[1], &thread->framerate.memory[0], sizeof(*thread->framerate.memory) * (FRAMERATE_MEMORY - 1));

	// Add new value at start:
	thread->framerate.memory[0] = *ts;

	// Adjust total number of timestamps in history buffer:
	if (thread->framerate.nmemory < FRAMERATE_MEMORY) {
		thread->framerate.nmemory++;
	}
	g_mutex_unlock(thread->framerate.mutex);
}

static void
framerate_estimator (struct mjv_thread *thread)
{
#define IN_USE  thread->framerate.nmemory
#define OLDEST  thread->framerate.memory[IN_USE - 1]
#define NEWEST  thread->framerate.memory[0]

	float diff_among_frames;
	float diff_with_now;
	struct timespec now;

	// This function must be run while the framerate
	// mutex is locked!!

	// No two values to compare yet?
	if (IN_USE <= 1) {
		thread->framerate.fps = -1.0;
		return;
	}
	// Compare oldest and newest values:
	diff_among_frames = timespec_diff(&NEWEST, &OLDEST);

	// Get the wall time:
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		thread->framerate.fps = (IN_USE - 1) / diff_among_frames;
		return;
	}
	// Calculate the difference between the last seen frame
	// and the wall time:
	diff_with_now = timespec_diff(&now, &NEWEST);

	// If this difference is smaller than the average FPS of
	// the frames among themselves, return the diff among frames:
	// If we have 5 frames, we have 4 intervals; hence IN_USE - 1
	if (diff_with_now < diff_among_frames) {
		thread->framerate.fps = (IN_USE - 1) / diff_among_frames;
		return;
	}
	// Else there has been a large time gap between the last received
	// frame and the now (the connection has lagged). If this is less than
	// 5 times the normal interval, we recalculate the FPS against the
	// current wall time, else return invalid:
	if (diff_with_now > diff_among_frames * 5.0) {
		thread->framerate.fps = -1.0;
	}
	else {
		diff_with_now = timespec_diff(&now, &OLDEST);
		thread->framerate.fps = IN_USE / diff_with_now;
	}

#undef NEWEST
#undef OLDEST
#undef IN_USE
}

static float
timespec_diff (struct timespec *new, struct timespec *old)
{
	time_t diff_sec = new->tv_sec - old->tv_sec;
	long diff_nsec = new->tv_nsec - old->tv_nsec;
	return diff_sec + ((float)diff_nsec / 1000000000.0);
}

static void
create_frame (struct mjv_thread *thread)
{
	thread->frame = gtk_frame_new(NULL);
	create_frame_toolbar(thread);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	GtkWidget *statusbar = create_frame_statusbar(thread);

	gtk_box_pack_start(GTK_BOX(vbox), thread->toolbar.toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), thread->canvas, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(thread->frame), vbox);
}

static void
create_frame_toolbar (struct mjv_thread *thread)
{
	GtkWidget *toolbar = gtk_toolbar_new();
	GtkToolItem *btn_record = gtk_toggle_tool_button_new_from_stock(GTK_STOCK_MEDIA_RECORD);
	GtkToolItem *btn_connect = gtk_tool_button_new_from_stock(GTK_STOCK_CONNECT);
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(btn_record), "Record");
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(btn_connect), "Connect");
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_connect, -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_record, -1);

	// Save these to thread object:
	thread->toolbar.toolbar = toolbar;
	thread->toolbar.btn_record = btn_record;
	thread->toolbar.btn_connect = btn_connect;
}

static GtkWidget *
create_frame_statusbar (struct mjv_thread *thread)
{
	GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);

	thread->statusbar.lbl_fps = gtk_label_new("0 fps");
	thread->statusbar.lbl_status = gtk_label_new("disconnected");
	thread->statusbar.lbl_framebuf = gtk_label_new("100/300");

	gtk_box_pack_start(GTK_BOX(hbox), thread->statusbar.lbl_status, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_vseparator_new(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), thread->statusbar.lbl_fps, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_vseparator_new(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), thread->statusbar.lbl_framebuf, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	return vbox;
}

static char *
status_string (struct mjv_thread *thread)
{
	switch (thread->state) {
		case STATE_DISCONNECTED: return "disconnected";
		case STATE_CONNECTING: return "connecting";
		case STATE_CONNECTED: return "connected";
	}
	return "unknown";
}

static void
update_state (struct mjv_thread *thread, enum state state)
{
	thread->state = state;
	gdk_threads_enter();
	gtk_label_set_text(GTK_LABEL(thread->statusbar.lbl_status), status_string(thread));
	gtk_widget_queue_draw(thread->statusbar.lbl_status);
	gdk_threads_leave();
}
