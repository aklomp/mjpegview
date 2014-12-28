CFLAGS += -std=c99 -O2 -Wall -Wextra -Werror
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`

.PHONY: all clean

PROGS = \
  test_mjv_filename \
  test_mjv_framerate \
  test_spinner

all: $(PROGS)

test_mjv_filename: test_mjv_filename.c ../mjv_filename.c
	$(CC) $(CFLAGS) -o $@ $<

test_mjv_framerate: test_mjv_framerate.c ../mjv_framerate.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -lrt -o $@ $<

test_spinner: test_spinner.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GTK_LDFLAGS) -o $@ $^

clean:
	rm -f $(PROGS)
