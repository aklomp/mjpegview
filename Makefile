CFLAGS      += -std=c99 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror
LDFLAGS     += -ljpeg -lrt -lconfig -lpthread
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`
GLIB_CFLAGS  = `pkg-config --cflags glib-2.0`
GLIB_LDFLAGS = `pkg-config --libs glib-2.0`

.PHONY: all clean

MJPEGVIEW_PROG = mjpegview
MJVSINGLE_PROG = mjvsingle
MJVMULTI_PROG = mjvmulti

all: $(MJPEGVIEW_PROG) $(MJVSINGLE_PROG) $(MJVMULTI_PROG)

# These object files do not depend on GLib or GTK+-2:
OBJS_PLAIN = \
  mjv_log.o \
  mjv_frame.o \
  mjv_config.o \
  mjv_source.o \
  mjv_grabber.o \
  filename.o \
  mjv_framebuf.o \
  mjv_framerate.o \
  mjvmulti.o \
  mjvsingle.o

# These object files depend only on GLib:
OBJS_GLIB = \
  mjpegview.o

# These object files depend on GLib and GTK+-2:
OBJS_GTK = \
  mjv_gui.o \
  mjv_thread.o \
  spinner.o

$(OBJS_PLAIN): %o: %c
	$(CC) $(CFLAGS) -c $^ -o $@

$(OBJS_GLIB): %.o: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c $^ -o $@

$(OBJS_GTK): %.o: %.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(GTK_CFLAGS) -c $^ -o $@

## mjpegview:

MJPEGVIEW_LDFLAGS = -ljpeg -lconfig -lpthread -lrt
MJPEGVIEW_OBJS = \
  mjv_log.o \
  mjv_frame.o \
  mjv_source.o \
  mjv_grabber.o \
  filename.o \
  mjv_framerate.o \
  mjpegview.o \
  mjv_config.o \
  mjv_framebuf.o \
  mjv_gui.o \
  mjv_thread.o \
  spinner.o

$(MJPEGVIEW_PROG): $(MJPEGVIEW_OBJS)
	$(CC) $(MJPEGVIEW_LDFLAGS) $(GLIB_LDFLAGS) $(GTK_LDFLAGS) $^ -o $@

## mjvsingle:

MJVSINGLE_LDFLAGS = -ljpeg -lrt
MJVSINGLE_OBJS = \
  mjvsingle.o \
  mjv_frame.o \
  mjv_source.o \
  mjv_grabber.o \
  filename.o \
  mjv_framerate.o

$(MJVSINGLE_PROG): $(MJVSINGLE_OBJS)
	$(CC) $(MJVSINGLE_LDFLAGS) $^ -o $@

## mjvmulti:

MJVMULTI_LDFLAGS = -ljpeg -lconfig -lpthread -lrt
MJVMULTI_OBJS = \
  mjvmulti.o \
  mjv_frame.o \
  mjv_config.o \
  mjv_source.o \
  mjv_grabber.o \
  filename.o \
  mjv_framerate.o

$(MJVMULTI_PROG): $(MJVMULTI_OBJS)
	$(CC) $(MJVMULTI_LDFLAGS) $^ -o $@

clean:
	rm -f \
	  $(OBJS_PLAIN) \
	  $(OBJS_GLIB) \
	  $(OBJS_GTK) \
	  $(MJPEGVIEW_PROG) \
	  $(MJVSINGLE_PROG) \
	  $(MJVMULTI_PROG)
