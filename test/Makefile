CFLAGS += -std=c99 -O2 -Wall -Wextra -Werror

.PHONY: all clean

PROGS = \
  test_mjv_filename

all: $(PROGS)

test_mjv_filename: test_mjv_filename.c ../mjv_filename.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)
