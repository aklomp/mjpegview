#include <stdbool.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "mjv_frame.h"
#include "mjv_source.h"
#include "mjv_thread.h"

// Cast a GList's data pointer to an object pointer:
#define MJV_SOURCE(x)	((struct mjv_source *)((x)->data))
#define MJV_THREAD(x)	((struct mjv_thread *)((x)->data))

static void cancel_all_threads (void);

static GList *thread_list = NULL;

static void
on_destroy (void)
{
	cancel_all_threads();
	gtk_main_quit();
}

// TODO GList is overkill, use GArray or GSList something
int
mjv_gui_main (int argc, char **argv, GList *sources)
{
	GList *link = NULL;

	if (!g_thread_supported()) {
		g_thread_init(NULL);
	}
	gdk_threads_init();
	gtk_init(&argc, &argv);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "mjpegview");

	// For each source, create a mjv_thread object, which we can run a little bit later on:
	for (link = g_list_first(sources); link; link = g_list_next(link))
	{
		struct mjv_source *source = MJV_SOURCE(link);
		struct mjv_thread *thread = mjv_thread_create(source);

		if (thread == NULL) {
			g_printerr("Error: could not create thread for source %s\n", mjv_source_get_name(source)) ;
			continue;
		}
		thread_list = g_list_append(thread_list, thread);
	}
	// Add all thread drawing areas to the box:
	// TODO: proper spacing algorithm:
	GtkWidget *box = gtk_hbox_new(FALSE, 0);
	for (link = g_list_first(thread_list); link; link = g_list_next(link))
	{
		struct mjv_thread *thread = MJV_THREAD(link);
		const GtkWidget *canvas = mjv_thread_get_canvas(thread);

		gtk_container_add(GTK_CONTAINER(box), (GtkWidget *)canvas);
	}
	gtk_container_add(GTK_CONTAINER(win), box);
	gtk_signal_connect(GTK_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);
	gtk_widget_show_all(win);

	// Create camera threads:
	for (link = g_list_first(thread_list); link; link = g_list_next(link)) {
		if (mjv_thread_run(MJV_THREAD(link)) == 0) {
			g_printerr("Could not create thread\n");
		}
	}
	// Enter main loop:
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();

	g_list_free_full(thread_list, (GDestroyNotify)(mjv_thread_destroy));

	return 0;
}

static void
cancel_all_threads (void)
{
	GList *link;

	gdk_threads_leave();
	for (link = g_list_first(thread_list); link; link = g_list_next(link)) {
		mjv_thread_cancel(MJV_THREAD(link));
	}
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
