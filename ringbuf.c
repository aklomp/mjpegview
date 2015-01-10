#include <stdlib.h>
#include <string.h>

struct ringbuf
{
	char *data;
	char *next;
	void (*on_destroy)(void *);
	unsigned int used;
	unsigned int size;
	size_t elemsize;
};

struct ringbuf *
ringbuf_create (unsigned int size, size_t elemsize, void (*on_destroy)(void *))
{
	struct ringbuf *rb;

	if ((rb = malloc(sizeof(*rb))) == NULL) {
		return NULL;
	}
	if ((rb->data = calloc(size, elemsize)) == NULL) {
		free(rb);
		return NULL;
	}
	rb->used = 0;
	rb->size = size;
	rb->next = rb->data;
	rb->elemsize = elemsize;
	rb->on_destroy = on_destroy;
	return rb;
}

void
ringbuf_destroy (struct ringbuf **rb)
{
	if (rb == NULL || *rb == NULL) {
		return;
	}
	// Call destroy callback on all elements of ringbuf:
	if ((*rb)->on_destroy) {
		for (char *p = (*rb)->data; p < (*rb)->data + (*rb)->size * (*rb)->elemsize; p += (*rb)->elemsize) {
			(*rb)->on_destroy(p);
		}
	}
	free((*rb)->data);
	free(*rb);
	*rb = NULL;
}

void
ringbuf_append (struct ringbuf *rb, void *datum)
{
	if (rb == NULL) {
		return;
	}
	// Delete the datum we're going to overwrite:
	if (rb->on_destroy) {
		rb->on_destroy(rb->next);
	}
	// Insert new datum:
	memcpy(rb->next, datum, rb->elemsize);

	// Increment 'next' pointer:
	rb->next += rb->elemsize;

	// Handle wraparound at end of array:
	if (rb->next == rb->data + (rb->size * rb->elemsize)) {
		rb->next = rb->data;
	}
	// Increment the usage counter:
	if (rb->used < rb->size) {
		rb->used++;
	}
}

void *
ringbuf_oldest (struct ringbuf *rb)
{
	if (rb == NULL) {
		return NULL;
	}
	// If ringbuf is not at capacity, oldest datum is #0; otherwise it's
	// the datum at 'next' that will be overwritten by the next datum:
	return (rb->used < rb->size)
		? rb->data
		: rb->next;
}

void *
ringbuf_newest (struct ringbuf *rb)
{
	if (rb == NULL) {
		return NULL;
	}
	// The newest datum is the one before 'next', handle wraparound:
	return (rb->next == rb->data)
		? rb->data + (rb->size - 1) * rb->elemsize
		: rb->next - rb->elemsize;
}

unsigned int
ringbuf_size (struct ringbuf *rb)
{
	return rb->size;
}

unsigned int
ringbuf_used (struct ringbuf *rb)
{
	return rb->used;
}
