#include "../mjv_framebuf.c"

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
	struct mjv_framebuf *fb;

	// Create the framebuf:
	if ((fb = mjv_framebuf_create(10)) == NULL) {
		return 1;
	}
	// Destroy the framebuf:
	mjv_framebuf_destroy(fb);
	return 0;
}

int
main ()
{
	int ret = 0;

	ret |= test_create();

	return ret;
}
