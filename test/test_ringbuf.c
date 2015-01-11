#include <string.h>

#include "../ringbuf.c"

static int
test_create ()
{
	struct ringbuf *rb;

	// Create the ringbuf:
	if ((rb = ringbuf_create(10, 1, NULL)) == NULL) {
		return 1;
	}
	// Destroy the ringbuf:
	ringbuf_destroy(&rb);
	return 0;
}

static int
test_insert_single ()
{
	struct ringbuf *rb;
	int ret = 0;

	// Create the ringbuf:
	if ((rb = ringbuf_create(10, sizeof(int), NULL)) == NULL) {
		return 1;
	}
	// Add a single element:
	ringbuf_append(rb, &((int){99}));

	// Check that the oldest element is our data pointer:
	if (*((int *)ringbuf_oldest(rb)) != 99) {
		ret = 1;
		goto out;
	}
	// Check that the newest element is our data pointer:
	if (*((int *)ringbuf_newest(rb)) != 99) {
		ret = 1;
		goto out;
	}
	// Check size and used:
	if (rb->size != 10) {
		ret = 1;
		goto out;
	}
	if (rb->used != 1) {
		ret = 1;
		goto out;
	}
	// Check that 'next' pointer is incremented:
	if (rb->next != rb->data + sizeof(int)) {
		ret = 1;
		goto out;
	}
	// Destroy the ringbuf:
out:	ringbuf_destroy(&rb);
	return ret;
}

static int
test_oldest ()
{
	struct testcase {
		int insert;
		int expect[4];
		int next;
		unsigned int used;
		int oldest;
		int newest;
	}
	testcases[] =
	{
	  { .insert = 1
	  , .expect = { 1, 0, 0, 0 }
	  , .used = 1
	  , .next = 1
	  , .oldest = 1
	  , .newest = 1
	  }
	, { .insert = 2
	  , .expect = { 1, 2, 0, 0 }
	  , .used = 2
	  , .next = 2
	  , .oldest = 1
	  , .newest = 2
	  }
	, { .insert = 3
	  , .expect = { 1, 2, 3, 0 }
	  , .used = 3
	  , .next = 3
	  , .oldest = 1
	  , .newest = 3
	  }
	, { .insert = 4
	  , .expect = { 1, 2, 3, 4 }
	  , .used = 4
	  , .next = 0
	  , .oldest = 1
	  , .newest = 4
	  }
	, { .insert = 5
	  , .expect = { 5, 2, 3, 4 }
	  , .used = 4
	  , .next = 1
	  , .oldest = 2
	  , .newest = 5
	  }
	} ;
	struct ringbuf *rb;
	int ret = 0;

	// Create ringbuf of 4 ints:
	if ((rb = ringbuf_create(4, sizeof(int), NULL)) == NULL) {
		return 1;
	}
	// All slots free, oldest and newest point to data:
	if (*((int *)ringbuf_oldest(rb)) != 0) {
		ret = 1;
		goto out;
	}
	if (*((int *)ringbuf_newest(rb)) != 0) {
		ret = 1;
		goto out;
	}
	// Loop over testcases:
	for (size_t i = 0; i < sizeof(testcases) / sizeof(struct testcase); i++) {
		ringbuf_append(rb, &testcases[i].insert);
		if (memcmp(rb->data, testcases[i].expect, sizeof(testcases[i].expect)) != 0) {
			ret = 1;
			goto out;
		}
		if (rb->next != rb->data + testcases[i].next * sizeof(int)) {
			ret = 1;
			goto out;
		}
		if (rb->used != testcases[i].used) {
			ret = 1;
			goto out;
		}
		if (*((int *)ringbuf_oldest(rb)) != testcases[i].oldest) {
			ret = 1;
			goto out;
		}
		if (*((int *)ringbuf_newest(rb)) != testcases[i].newest) {
			ret = 1;
			goto out;
		}
	}
out:	ringbuf_destroy(&rb);
	return ret;
}

int
main ()
{
	int ret = 0;

	ret |= test_create();
	ret |= test_insert_single();
	ret |= test_oldest();

	return ret;
}
