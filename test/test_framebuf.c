#include "../framebuf.c"

// Stub functions:
void
mjv_frame_destroy (struct mjv_frame **const f)
{
	*f = NULL;
}

struct timespec *
mjv_frame_get_timestamp (const struct mjv_frame *const f)
{
	(void)f;
	return NULL;
}

void
log_debug (const char *const fmt, ...)
{
	(void)fmt;
}

// Test functions:

static int
test_create ()
{
	struct framebuf *fb;

	// Create the framebuf:
	if ((fb = framebuf_create(10)) == NULL) {
		return 1;
	}
	// Destroy the framebuf:
	framebuf_destroy(fb);
	return 0;
}

static int
test_insert_single ()
{
	struct framebuf *fb;
	struct mjv_frame *data = (struct mjv_frame *)"hello";

	// Create the framebuf:
	if ((fb = framebuf_create(10)) == NULL) {
		return 1;
	}
	// Add a single element:
	framebuf_append(fb, (struct mjv_frame *)data);

	// Check that the oldest element is our data pointer:
	if (*oldest(fb) != data) {
		return 1;
	}
	// Check that the newest element is our data pointer:
	if (*newest(fb) != data) {
		return 1;
	}
	// Check size and used:
	if (fb->size != 10) {
		return 1;
	}
	if (fb->used != 1) {
		return 1;
	}
	// Check that 'next' pointer is incremented:
	if (fb->next != fb->frames + 1) {
		return 1;
	}
	// Destroy the framebuf:
	framebuf_destroy(fb);
	return 0;
}

static int
test_oldest ()
{
	struct mjv_frame *frames[4];

	// Create static object:
	struct framebuf fb =
		{ .frames = frames
		, .next = frames
		, .size = 4
		, .used = 0
		} ;

	// All slots free, oldest and newest point to frames:
	if (oldest(&fb) != frames) {
		return 1;
	}
	if (newest(&fb) != frames) {
		return 1;
	}
	// Simulate 2 slots used:
	fb.used = 2;
	fb.next = &frames[2];

	if (oldest(&fb) != &frames[0]) {
		return 1;
	}
	if (newest(&fb) != &frames[1]) {
		return 1;
	}
	// Simulate 4 slots used, 'next' is first element:
	fb.used = fb.size;
	fb.next = &frames[0];

	if (oldest(&fb) != &frames[0]) {
		return 1;
	}
	if (newest(&fb) != &frames[3]) {
		return 1;
	}
	return 0;
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
