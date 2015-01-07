#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "mjv_log.h"
#include "mjv_frame.h"

struct mjv_framebuf {
	unsigned int used;
	unsigned int capacity;
	GList *frames;
};

#define MJV_FRAME(x)	((struct mjv_frame *)((x)->data))

struct mjv_framebuf *
mjv_framebuf_create (unsigned int capacity)
{
	struct mjv_framebuf *framebuf;

	if ((framebuf = malloc(sizeof(*framebuf))) == NULL) {
		return NULL;
	}
	framebuf->used = 0;
	framebuf->capacity = capacity;
	framebuf->frames = NULL;
	return framebuf;
}

void
mjv_framebuf_destroy (struct mjv_framebuf *framebuf)
{
	GList *link;

	if (framebuf == NULL) {
		return;
	}
	log_debug("Destroying framebuf with %u members (capacity %u)\n", g_list_length(framebuf->frames), framebuf->capacity);

	// Loop over the list, destroy all frames:
	for (link = g_list_first(framebuf->frames); link; link = g_list_next(link)) {
		mjv_frame_destroy((struct mjv_frame **)(&link->data));
	}
	// Destroy list itself:
	g_list_free(framebuf->frames);
	free(framebuf);
}

bool
mjv_framebuf_append (struct mjv_framebuf *framebuf, struct mjv_frame *frame)
{
	GList *first;

	if (framebuf == NULL) {
		return false;
	}
	// If this frame will push the framebuf over capacity,
	// delete the first frame:
	if (framebuf->used == framebuf->capacity) {
		first = g_list_first(framebuf->frames);
		mjv_frame_destroy((struct mjv_frame **)(&first->data));
		framebuf->frames = g_list_delete_link(framebuf->frames, first);
	}
	else {
		framebuf->used++;
	}
	// Append new node:
	framebuf->frames = g_list_append(framebuf->frames, frame);
	return true;
}

char *
mjv_framebuf_status_string (const struct mjv_framebuf *const f)
{
	// Returns a pointer to a string containing status information about
	// this framebuffer object. Caller is responsible for g_free()'ing the
	// return value.

	int days = 0;
	int hours = 0;
	int minutes = 0;
	GList *newest;
	GList *oldest;

	if (f == NULL) {
		return NULL;
	}
	// FIXME: this is all not very efficient.
	if ((oldest = g_list_first(f->frames)) == NULL) {
		return NULL;
	}
	if ((newest = g_list_last(f->frames)) == NULL) {
		return NULL;
	}
	struct timespec ts_oldest = *mjv_frame_get_timestamp(MJV_FRAME(oldest));
	struct timespec ts_newest = *mjv_frame_get_timestamp(MJV_FRAME(newest));

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
		return (snprintf(buf, sizeof(buf), "%u/%u, %dd %dh %dm %ds", f->used, f->capacity, days, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (hours > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dh %dm %ds", f->used, f->capacity, hours, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	if (minutes > 0) {
		return (snprintf(buf, sizeof(buf), "%u/%u, %dm %ds", f->used, f->capacity, minutes, seconds) > 0)
			? strndup(buf, sizeof(buf))
			: NULL;
	}
	return (snprintf(buf, sizeof(buf), "%u/%u, %ds", f->used, f->capacity, seconds) > 0)
		? strndup(buf, sizeof(buf))
		: NULL;
}
