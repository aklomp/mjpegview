#include <stdbool.h>
#include <glib.h>
#include <assert.h>

#include "mjv_frame.h"

struct mjv_framebuf {
	guint capacity;
	GList *frames;
};

static void
destroy_frame (gpointer data, gpointer user_data)
{
	(void)user_data;

	// Trivial helper function; user_data is always NULL.
	g_assert(data != NULL);
	mjv_frame_destroy(data);
}

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
	g_assert(framebuf != NULL);

	// Loop over the list, destroy all frames:
	g_list_foreach(framebuf->frames, destroy_frame, NULL);

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
		mjv_frame_destroy(first->data);
		framebuf->frames = g_list_delete_link(framebuf->frames, first);
	}
	// Append new node:
	framebuf->frames = g_list_append(framebuf->frames, frame);
	return true;
}
