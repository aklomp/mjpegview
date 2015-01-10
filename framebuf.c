// Unlock `struct timespec` and strndup():
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mjv_log.h"
#include "mjv_frame.h"
#include "ringbuf.h"

struct framebuf {
	struct ringbuf *rb;
};

static void
on_frame_destroy (void *datum)
{
	mjv_frame_destroy((struct mjv_frame **)datum);
}

struct framebuf *
framebuf_create (unsigned int size)
{
	struct framebuf *fb;

	if ((fb = malloc(sizeof(*fb))) == NULL) {
		return NULL;
	}
	// Allocate ring buffer to hold references to the frames:
	if ((fb->rb = ringbuf_create(size, sizeof(struct mjv_frame *), on_frame_destroy)) == NULL) {
		free(fb);
		return NULL;
	}
	return fb;
}

void
framebuf_destroy (struct framebuf *fb)
{
	if (fb == NULL) {
		return;
	}
	log_debug("Destroying framebuf with %u members (capacity %u)\n", ringbuf_used(fb->rb), ringbuf_size(fb->rb));
	ringbuf_destroy(&fb->rb);
	free(fb);
}

void
framebuf_append (struct framebuf *fb, struct mjv_frame *frame)
{
	ringbuf_append(fb->rb, &frame);
}

char *
framebuf_status_string (const struct framebuf *const fb)
{
	// Returns a pointer to a string containing status information about
	// this framebuffer object. Caller is responsible for g_free()'ing the
	// return value.

	int days = 0;
	int hours = 0;
	int minutes = 0;

	if (fb == NULL) {
		return NULL;
	}
	struct mjv_frame *old = *((struct mjv_frame **)ringbuf_oldest(fb->rb));
	struct mjv_frame *new = *((struct mjv_frame **)ringbuf_newest(fb->rb));

	if (old == NULL || new == NULL) {
		return NULL;
	}
	struct timespec *ts_old = mjv_frame_get_timestamp(old);
	struct timespec *ts_new = mjv_frame_get_timestamp(new);

	// Find time difference between oldest and newest frames:
	int seconds = ts_new->tv_sec - ts_old->tv_sec;

	if (seconds >= 60) {
		minutes = seconds / 60;
		seconds %= 60;
	}
	if (minutes >= 60) {
		hours = minutes / 60;
		minutes %= 60;
	}
	if (hours >= 24) {
		days = hours / 24;
		hours %= 24;
	}
	// This buffer should be large enough to contain any string formatted below;
	// we only format integers, which are at most 10 characters or so.
	char buf[100];
	unsigned int used = ringbuf_used(fb->rb);
	unsigned int size = ringbuf_size(fb->rb);

	if (days > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dd %dh %dm %ds", used, size, days, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (hours > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dh %dm %ds", used, size, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (minutes > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dm %ds", used, size, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	return (snprintf(buf, sizeof(buf), "%u/%u, %ds", used, size, seconds) > 0)
		? strndup(buf, sizeof(buf))
		: NULL;
}
