#include <stdbool.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "mjv_log.h"
#include "mjv_config.h"
#include "mjv_source.h"
#include "mjv_thread.h"

// Cast a GList's data pointer to an object pointer:
#define MJV_THREAD(x)    ((struct mjv_thread *)((x)->data))

static GList *thread_list = NULL;

static void
cancel_all_threads (void)
{
	GList *link;

	gdk_threads_leave();
	for (link = g_list_first(thread_list); link; link = g_list_next(link)) {
		mjv_thread_cancel(MJV_THREAD(link));
	}
}

static void
on_destroy (void)
{
	cancel_all_threads();
	gtk_main_quit();
}

// TODO GList is overkill, use GArray or GSList something
int
mjv_gui_main (int argc, char **argv, struct mjv_config *config)
{
	GList *link = NULL;
	struct mjv_source *s;

	gdk_threads_init();
	gtk_init(&argc, &argv);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "mjpegview");

	// For each config source, create a mjv_thread object,
	// which we can run a little bit later on:
	for (s = mjv_config_source_first(config); s; s = mjv_config_source_next(config))
	{
		struct mjv_thread *thread = mjv_thread_create(s);

		if (thread == NULL) {
			log_error("Error: could not create thread for source %s\n", mjv_source_get_name(s));
			continue;
		}
		thread_list = g_list_append(thread_list, thread);
	}
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);

	// Add all thread drawing areas to the box:
	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
	for (link = g_list_first(thread_list); link; link = g_list_next(link))
	{
		struct mjv_thread *thread = MJV_THREAD(link);
		GtkWidget *thread_area = mjv_thread_create_area(thread);

		gtk_container_add(GTK_CONTAINER(hbox), thread_area);
	}
	gtk_container_add(GTK_CONTAINER(vbox), hbox);

	gtk_container_add(GTK_CONTAINER(win), vbox);
	gtk_signal_connect(GTK_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);
	gtk_widget_show_all(win);

	// Run camera threads:
	for (link = g_list_first(thread_list); link; link = g_list_next(link)) {
		if (mjv_thread_run(MJV_THREAD(link)) == 0) {
			log_error("Could not create thread\n");
		}
	}
	// Enter main loop:
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();

	g_list_free_full(thread_list, (GDestroyNotify)(mjv_thread_destroy));

	return 0;
}
