#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "mjv_frame.h"

struct mjv_framebuf {
	unsigned int capacity;
	unsigned int nodecount;
	struct mjv_framebuf_node *first;
	struct mjv_framebuf_node *last;
};

struct mjv_framebuf_node {
	struct mjv_frame *frame;
	struct mjv_framebuf_node *prev;
	struct mjv_framebuf_node *next;
};

static void
mjv_framebuf_node_destroy (struct mjv_framebuf *framebuf, struct mjv_framebuf_node *node)
{
	assert(framebuf != NULL);
	assert(node != NULL);

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}
	if (node->prev != NULL) {
		node->prev->next = node->next;
	}
	if (node == framebuf->first) {
		framebuf->first = node->next;
	}
	mjv_frame_destroy(node->frame);
	framebuf->nodecount--;
	free(node);
}

struct mjv_framebuf *
mjv_framebuf_create (unsigned int capacity)
{
	struct mjv_framebuf *framebuf;

	if ((framebuf = malloc(sizeof(*framebuf))) == NULL) {
		return NULL;
	}
	framebuf->nodecount = 0;
	framebuf->capacity = capacity;
	framebuf->first = NULL;
	framebuf->last = NULL;
	return framebuf;
}

void
mjv_framebuf_destroy (struct mjv_framebuf *framebuf)
{
	struct mjv_framebuf_node *node;
	struct mjv_framebuf_node *next;

	if (framebuf == NULL) {
		return;
	}
	node = framebuf->first;
	while (node != NULL) {
		next = node->next;
		mjv_framebuf_node_destroy(framebuf, node);
		node = next;
	}
	free(framebuf);
}

bool
mjv_framebuf_frame_append (struct mjv_framebuf *framebuf, struct mjv_frame *frame)
{
	struct mjv_framebuf_node *node;

	assert(framebuf != NULL);
	assert(frame != NULL);

	if ((node = malloc(sizeof(*node))) == NULL) {
		return false;
	}
	if (framebuf->first == NULL) {
		framebuf->first = node;
	}
	node->frame = frame;
	node->prev = framebuf->last;
	node->next = NULL;
	framebuf->last = node;
	framebuf->nodecount++;

	// If over capacity, destroy the first node:
	if (framebuf->nodecount > framebuf->capacity) {
		if (framebuf->first != node) {
			mjv_framebuf_node_destroy(framebuf, framebuf->first);
		}
	}
	return true;
}
