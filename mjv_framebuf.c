#include <stdbool.h>
#include <glib.h>
#include <assert.h>

#include "mjv_frame.h"

struct mjv_framebuf {
	guint capacity;
	GList *frames;
};

#define MJV_FRAME(x)	((struct mjv_frame *)((x)->data))

struct mjv_framebuf *
mjv_framebuf_create (guint capacity)
{
	struct mjv_framebuf *framebuf;

	framebuf = g_malloc(sizeof(*framebuf));
	framebuf->capacity = capacity;
	framebuf->frames = NULL;
	return framebuf;
}

void
mjv_framebuf_destroy (struct mjv_framebuf *framebuf)
{
	GList *link;

	g_assert(framebuf != NULL);

	// Loop over the list, destroy all frames:
	for (link = g_list_first(framebuf->frames); link; link = g_list_next(link)) {
		mjv_frame_destroy(MJV_FRAME(link));
	}

	// Destroy list itself:
	g_list_free(framebuf->frames);
	g_free(framebuf);
}

bool
mjv_framebuf_frame_append (struct mjv_framebuf *framebuf, struct mjv_frame *frame)
{
	GList *first;

	assert(framebuf != NULL);
	assert(frame != NULL);

	// If this frame will push the framebuf over capacity,
	// delete the first frame:
	if (g_list_length(framebuf->frames) >= framebuf->capacity) {
		first = g_list_first(framebuf->frames);
		mjv_frame_destroy(MJV_FRAME(first));
		framebuf->frames = g_list_delete_link(framebuf->frames, first);
	}
	// Append new node:
	framebuf->frames = g_list_append(framebuf->frames, frame);
	return true;
}
