#include <gtk/gtk.h>

static void
on_expose (GtkWidget *darea)
{
	cairo_t *cr = gdk_cairo_create(darea->window);
	cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
	cairo_paint(cr);

	cairo_destroy(cr);
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

	// Connect signals:
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(darea, "expose-event", G_CALLBACK(on_expose), NULL);

	// Show window:
	gtk_widget_show_all(win);

	// Yield:
	gtk_main();

	return 0;
}
