#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "frame.h"
#include "framebuf.h"
#include "framerate.h"
#include "source.h"
#include "mjv_grabber.h"
#include "mjv_thread.h"
#include "selfpipe.h"
#include "spinner.h"

enum state
{ STATE_DISCONNECTED
, STATE_CONNECTING
, STATE_CONNECTED
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
	GMutex    mutex;
	GdkPixbuf *pixbuf;
	GtkWidget *frame;
	GtkWidget *canvas;
	int selfpipe_readfd;
	int selfpipe_writefd;
	unsigned int width;
	unsigned int height;
	unsigned int blinker;
	struct spinner *spinner;
	struct source *source;
	struct mjv_grabber *grabber;
	struct framebuf *framebuf;
	struct toolbar toolbar;
	struct statusbar statusbar;
	enum state state;

	struct framerate *framerate;
	GMutex framerate_mutex;
	pthread_t framerate_pthread;

	pthread_t      pthread;
	pthread_attr_t pthread_attr;
};

#define BLINKER_ALPHA	0.3
#define BLINKER_HEIGHT	8

static void *thread_main (void *);
static void callback_got_frame (struct frame *, void *);
static void draw_blinker (cairo_t *, int, int, int);

static void framerate_thread_run (struct mjv_thread *);
static void framerate_thread_kill (struct mjv_thread *);
static void *framerate_thread_main (void *);

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
	const char *source_name;
	struct mjv_thread *t = (struct mjv_thread *)(user_data);
	(void)event;

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
	g_mutex_lock(&t->mutex);
	t->cairo = gdk_cairo_create(widget->window);

	if (t->pixbuf == NULL) {
		cairo_set_source_rgb(t->cairo, 0.1, 0.1, 0.1);
	}
	else {
		gdk_cairo_set_source_pixbuf(t->cairo, t->pixbuf, 0, 0);
	}
	cairo_paint(t->cairo);

	if ((source_name = source_get_name(t->source)) != NULL) {
		print_source_name(t->cairo, source_name);
	}
	if (t->state == STATE_CONNECTING) {
		spinner_repaint(t->spinner, t->cairo, widget->allocation.width / 2, widget->allocation.height / 2);
	}
	else if (t->state == STATE_CONNECTED) {
		draw_blinker(t->cairo, 4, t->height - 4 - BLINKER_HEIGHT, t->blinker);
	}
	cairo_destroy(t->cairo);
	g_mutex_unlock(&t->mutex);
	return TRUE;
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

struct mjv_thread *
mjv_thread_create (struct source *source)
{
	// This function creates a thread object, but does not run it.
	// To run, call mjv_thread_run on the thread object.

	struct mjv_thread *t;

	if ((t = calloc(1, sizeof(*t))) == NULL) {
		goto err_0;
	}
	if ((t->framerate = framerate_create(15)) == NULL) {
		goto err_1;
	}
	if ((t->framebuf = framebuf_create(50)) == NULL) {
		goto err_2;
	}
	// Open a pipe pair to use in the self-pipe trick. When we write
	// a byte to the pipe, the grabber knows to quit gracefully:
	if (selfpipe_pair(&t->selfpipe_readfd, &t->selfpipe_writefd) == false) {
		goto err_3;
	}
	t->width   = 640;
	t->height  = 480;
	t->source  = source;
	t->grabber = NULL;
	t->canvas  = gtk_drawing_area_new();
	t->state   = STATE_DISCONNECTED;
	t->spinner = NULL;

	g_mutex_init(&t->mutex);
	g_mutex_init(&t->framerate_mutex);

	pthread_attr_init(&t->pthread_attr);
	pthread_attr_setdetachstate(&t->pthread_attr, PTHREAD_CREATE_JOINABLE);

	gtk_widget_set_size_request(t->canvas, t->width, t->height);
	gtk_signal_connect(GTK_OBJECT(t->canvas), "expose_event", GTK_SIGNAL_FUNC(canvas_repaint), t);

	return t;

err_3:	framebuf_destroy(&t->framebuf);
err_2:	framerate_destroy(&t->framerate);
err_1:	free(t);
err_0:	return NULL;
}

void
mjv_thread_destroy (struct mjv_thread *t)
{
	g_assert(t != NULL);

	spinner_destroy(&t->spinner);
	g_mutex_clear(&t->mutex);
	g_mutex_clear(&t->framerate_mutex);
	pthread_attr_destroy(&t->pthread_attr);
	mjv_grabber_destroy(&t->grabber);
	framebuf_destroy(&t->framebuf);
	framerate_destroy(&t->framerate);
	if (t->pixbuf != NULL) {
		g_object_unref(t->pixbuf);
	}
	selfpipe_write_close(&t->selfpipe_writefd);
	selfpipe_read_close(&t->selfpipe_readfd);
	free(t);
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
	// Terminate a thread by sending a byte of data through the write end
	// of the self-pipe. The grabber catches this and exits gracefully:
	selfpipe_write_close(&t->selfpipe_writefd);

	if (pthread_join(t->pthread, NULL) != 0) {
		return false;
	}
	return true;
}

GtkWidget *
mjv_thread_create_area (struct mjv_thread *t)
{
	// Create the frame for this thread.
	create_frame(t);
	return t->frame;
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

static void
on_spinner_tick (void *userdata)
{
	gdk_threads_enter();
	gtk_widget_queue_draw(GTK_WIDGET(userdata));
	gdk_threads_leave();
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

static void *
thread_main (void *user_data)
{
	struct mjv_thread *t = (struct mjv_thread *)user_data;

	// Try to connect:
	t->spinner = spinner_create(on_spinner_tick, t->canvas);
	update_state(t, STATE_CONNECTING);

	// Open source file descriptor:
	if (t->source->open(t->source) == false) {
		spinner_destroy(&t->spinner);
		update_state(t, STATE_DISCONNECTED);
		return NULL;
	}
	if ((t->grabber = mjv_grabber_create(t->source)) == NULL) {
		spinner_destroy(&t->spinner);
		update_state(t, STATE_DISCONNECTED);
		return NULL;
	}
	mjv_grabber_set_callback(t->grabber, &callback_got_frame, (void *)t);
	mjv_grabber_set_selfpipe(t->grabber, t->selfpipe_readfd);

	// We are connected:
	update_state(t, STATE_CONNECTED);
	spinner_destroy(&t->spinner);
	framerate_thread_run(t);

	// Stay in this function till it terminates;
	// meanwhile we get frames back through callback_got_frame():
	mjv_grabber_run(t->grabber);

	// We are disconnected:
	mjv_grabber_destroy(&t->grabber);
	update_state(t, STATE_DISCONNECTED);
	framerate_thread_kill(t);
	return NULL;
}

static void
destroy_pixels (guchar *pixels, gpointer data)
{
	(void)data;
	free(pixels);
}

static void
update_framebuf_label (struct mjv_thread *thread)
{
	char *s = framebuf_status_string(thread->framebuf);
	gdk_threads_enter();
	gtk_label_set_text(GTK_LABEL(thread->statusbar.lbl_framebuf), s);
	gtk_widget_queue_draw(thread->statusbar.lbl_framebuf);
	gdk_threads_leave();
	free(s);
}

static void
callback_got_frame (struct frame *frame, void *user_data)
{
	unsigned char *pixels;
	struct mjv_thread *thread = (struct mjv_thread *)(user_data);

	g_assert(frame != NULL);
	g_assert(thread != NULL);

	// Convert from JPEG to pixbuf:
	if ((pixels = frame_to_pixbuf(frame)) == NULL) {
		frame_destroy(&frame);
		return;
	}
	unsigned int width = frame_get_width(frame);
	unsigned int height = frame_get_height(frame);
	unsigned int row_stride = frame_get_row_stride(frame);

	gdk_threads_enter();
	g_mutex_lock(&thread->mutex);

	g_assert(width > 0);
	g_assert(height > 0);
	g_assert(row_stride > 0);

	thread->width  = width;
	thread->height = height;

	if ((int)height != thread->canvas->allocation.height
	 || (int)width != thread->canvas->allocation.width) {
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

	g_mutex_lock(&thread->framerate_mutex);
	framerate_insert_datapoint(thread->framerate, frame_get_timestamp(frame));
	g_mutex_unlock(&thread->framerate_mutex);

	g_mutex_unlock(&thread->mutex);
	gdk_threads_leave();

	framebuf_append(thread->framebuf, frame);
	update_framebuf_label(thread);
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
framerate_thread_run (struct mjv_thread *t)
{
	// TODO: error handling, ...
	pthread_create(&t->framerate_pthread, NULL, framerate_thread_main, t);
}

static void
framerate_thread_kill (struct mjv_thread *t)
{
	pthread_cancel(t->framerate_pthread);
}

static void *
framerate_thread_main (void *user_data)
{
	float fps;
	char buf[20];
	struct mjv_thread *t = (struct mjv_thread *)user_data;

	pthread_detach(pthread_self());

	for (;;)
	{
		// Get FPS value:
		g_mutex_lock(&t->framerate_mutex);
		fps = framerate_estimate(t->framerate);
		g_mutex_unlock(&t->framerate_mutex);

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
