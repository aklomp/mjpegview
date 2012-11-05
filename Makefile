CFLAGS      += -std=c99 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror
LDFLAGS     += -ljpeg -lrt -lconfig -lpthread
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`
GLIB_CFLAGS  = `pkg-config --cflags glib-2.0`
GLIB_LDFLAGS = `pkg-config --libs glib-2.0`

.PHONY: clean

PROG = mjpegview
MJVSINGLE_PROG = mjvsingle

all: $(PROG) $(MJVSINGLE_PROG)

# These object files do not depend on GLib or GTK+-2:
OBJS = \
  mjv_frame.o \
  mjv_grabber.o \
  mjv_log.o \
  mjv_source.o

# These object files depend only on GLib:
OBJS_GLIB = \
  mjv_config.o \
  mjv_framebuf.o \
  mjv_main.o

# These object files depend on GLib and GTK+-2:
OBJS_GTK = \
  mjv_gui.o \
  mjv_thread.o

$(PROG): $(OBJS) $(OBJS_GLIB) $(OBJS_GTK)
	$(CC) $(LDFLAGS) $(GLIB_LDFLAGS) $(GTK_LDFLAGS) $^ -o $@

$(OBJS): %o: %c
	$(CC) $(CFLAGS) -c $^ -o $@

$(OBJS_GLIB): %.o: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c $^ -o $@

$(OBJS_GTK): %.o: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(GTK_CFLAGS) -c $^ -o $@

MJVSINGLE_OBJS = mjvsingle.o
MJVSINGLE_CFLAGS = -O2 -Werror -Wall -Wextra -fomit-frame-pointer -pipe
MJVSINGLE_LDFLAGS = -ljpeg -lrt

$(MJVSINGLE_PROG): $(MJVSINGLE_OBJS) mjv_source.o mjv_grabber.o mjv_frame.o
	$(CC) $(MJVSINGLE_LDFLAGS) $^ -o $@

$(MJVSINGLE_OBJS): %.o: %.c
	$(CC) $(MJVSINGLE_CFLAGS) -c $^ -o $@

clean:
	rm -f $(PROG) $(OBJS) $(OBJS_GLIB) $(OBJS_GTK) $(MJVSINGLE_OBJS) $(MJVSINGLE_PROG)
