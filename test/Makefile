CFLAGS += -std=c99 -O2 -Wall -Wextra -Werror
GTK_CFLAGS   = `pkg-config --cflags gtk+-2.0`
GTK_LDFLAGS  = `pkg-config --libs gtk+-2.0`

.PHONY: test clean

PROGS = \
  test_filename \
  test_mjv_framerate \
  test_ringbuf \
  test_spinner

test: clean test_filename test_mjv_framerate test_ringbuf
	./test_filename
	./test_mjv_framerate
	./test_ringbuf

test_filename: test_filename.c ../filename.c
	$(CC) $(CFLAGS) -o $@ $<

test_mjv_framerate: test_mjv_framerate.c ../mjv_framerate.c ../ringbuf.o
	$(CC) $(CFLAGS) -D_GNU_SOURCE -lrt ../ringbuf.o -o $@ $<

test_ringbuf: test_ringbuf.c ../ringbuf.c
	$(CC) $(CFLAGS) -o $@ $<

test_spinner: test_spinner.c ../spinner.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GTK_LDFLAGS) -pthread -o $@ $^

../%.o:
	make -C .. $*.o

clean:
	rm -f $(PROGS)
