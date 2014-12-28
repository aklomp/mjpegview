#include <gtk/gtk.h>

#include "../spinner.h"

static void
on_expose (GtkWidget *darea, GdkEventExpose *event, struct spinner *spinner)
{
	cairo_t *cr = gdk_cairo_create(darea->window);
	cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
	cairo_paint(cr);

	spinner_repaint(spinner, cr, event->area.width / 2, event->area.height / 2);

	cairo_destroy(cr);
}

static void
on_destroy (GtkWidget *widget, struct spinner *spinner)
{
	(void)widget;

	spinner_destroy(&spinner);
	gtk_main_quit();
}

static void
on_spinner_tick (void *userdata)
{
	gtk_widget_queue_draw(GTK_WIDGET(userdata));
}

int
main (int argc, char **argv)
{
	gtk_init(&argc, &argv);

	// Create simple GTK window with drawing area:
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *darea = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(win), darea);
	gtk_window_set_default_size(GTK_WINDOW(win), 200, 200);

	// Init spinner:
	struct spinner *spinner = spinner_create(on_spinner_tick, darea);

	// Connect signals:
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), spinner);
	g_signal_connect(darea, "expose-event", G_CALLBACK(on_expose), spinner);

	// Show window:
	gtk_widget_show_all(win);

	// Yield:
	gtk_main();

	return 0;
}
