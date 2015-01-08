#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mjv_log.h"
#include "mjv_frame.h"

struct mjv_framebuf {
	struct mjv_frame **frames;
	struct mjv_frame **next;
	unsigned int used;
	unsigned int size;
};

struct mjv_framebuf *
mjv_framebuf_create (unsigned int size)
{
	struct mjv_framebuf *fb;

	if ((fb = malloc(sizeof(*fb))) == NULL) {
		return NULL;
	}
	// Allocate a pointer array to hold references to the frames:
	if ((fb->frames = calloc(size, sizeof(*fb->frames))) == NULL) {
		free(fb);
		return NULL;
	}
	fb->used = 0;
	fb->size = size;
	fb->next = fb->frames;
	return fb;
}

void
mjv_framebuf_destroy (struct mjv_framebuf *fb)
{
	if (fb == NULL) {
		return;
	}
	log_debug("Destroying framebuf with %u members (capacity %u)\n", fb->used, fb->size);

	// Loop over the frame array, free every entry:
	for (struct mjv_frame **f = fb->frames; f < fb->frames + fb->size; f++) {
		mjv_frame_destroy(f);
	}
	free(fb->frames);
	free(fb);
}

bool
mjv_framebuf_append (struct mjv_framebuf *fb, struct mjv_frame *frame)
{
	if (fb == NULL) {
		return false;
	}
	// If this frame will push the framebuf over capacity,
	// delete the oldest frame (which is about to be overwritten):
	if (fb->used == fb->size) {
		mjv_frame_destroy(fb->next);
	}
	else {
		fb->used++;
	}
	// Append new node, increment 'next' pointer:
	*fb->next++ = frame;

	// Handle wraparound at end of array:
	if (fb->next == fb->frames + fb->size) {
		fb->next = fb->frames;
	}
	return true;
}

char *
mjv_framebuf_status_string (const struct mjv_framebuf *const fb)
{
	// Returns a pointer to a string containing status information about
	// this framebuffer object. Caller is responsible for g_free()'ing the
	// return value.

	int days = 0;
	int hours = 0;
	int minutes = 0;
	struct mjv_frame **newest;
	struct mjv_frame **oldest;

	if (fb == NULL) {
		return NULL;
	}
	// If framebuf is not at capacity, oldest frame is #0; otherwise it's
	// the frame at 'next' that will be overwritten by the next frame:
	oldest = (fb->used < fb->size)
		? fb->frames
		: fb->next;

	// The newest frame is the frame before 'next', if we have at least one
	// frame:
	newest = fb->next;
	if (fb->used > 0) {
		if (newest == fb->frames) {
			newest += fb->size;
		}
		newest--;
	}
	if (*oldest == NULL || *newest == NULL) {
		return NULL;
	}
	struct timespec ts_oldest = *mjv_frame_get_timestamp(*oldest);
	struct timespec ts_newest = *mjv_frame_get_timestamp(*newest);

	// Find time difference between oldest and newest frames:
	int seconds = ts_newest.tv_sec - ts_oldest.tv_sec;

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

	if (days > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dd %dh %dm %ds", fb->used, fb->size, days, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (hours > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dh %dm %ds", fb->used, fb->size, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (minutes > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dm %ds", fb->used, fb->size, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	return (snprintf(buf, sizeof(buf), "%u/%u, %ds", fb->used, fb->size, seconds) > 0)
		? strndup(buf, sizeof(buf))
		: NULL;
}
