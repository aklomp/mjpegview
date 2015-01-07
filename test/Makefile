CFLAGS += -std=c99 -O2 -Wall -Wextra -Werror
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`

.PHONY: test clean

PROGS = \
  test_filename \
  test_mjv_framerate \
  test_spinner

test: clean test_filename test_mjv_framerate
	./test_filename
	./test_mjv_framerate

test_filename: test_filename.c ../filename.c
	$(CC) $(CFLAGS) -o $@ $<

test_mjv_framerate: test_mjv_framerate.c ../mjv_framerate.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -lrt -o $@ $<

test_spinner: test_spinner.c ../spinner.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GTK_LDFLAGS) -pthread -o $@ $^

clean:
	rm -f $(PROGS)
