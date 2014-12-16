CFLAGS += -std=c99 -O2 -Wall -Wextra -Werror

.PHONY: all clean

PROGS = \
  test_mjv_filename \
  test_mjv_framerate

all: $(PROGS)

test_mjv_filename: test_mjv_filename.c ../mjv_filename.c
	$(CC) $(CFLAGS) -o $@ $<

test_mjv_framerate: test_mjv_framerate.c ../mjv_framerate.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -lrt -o $@ $<

clean:
	rm -f $(PROGS)
