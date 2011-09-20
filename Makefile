CFLAGS      += -std=c99 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror
LDFLAGS     += -ljpeg -lrt -lconfig -lpthread
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`

.PHONY: clean

PROG = mjpegview

OBJS = \
  mjv_config.o \
  mjv_frame.o \
  mjv_framebuf.o \
  mjv_gui.o \
  mjv_main.o \
  mjv_source.o \
  mjv_thread.o

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $(GTK_LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $^ -o $@

clean:
	rm -f $(PROG) $(OBJS)
