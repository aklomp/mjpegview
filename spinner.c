#include <stdlib.h>
#include <pthread.h>
#include <cairo.h>

struct spinner
{
	void (*on_tick)(void *);
	void *userdata;
	unsigned int step;
	unsigned int iters;
	struct timespec start;
	pthread_t pthread;
};

struct spinner *
spinner_create (void (*on_tick)(void *), void *userdata)
{
	struct spinner *s;

	if ((s = malloc(sizeof(*s))) == NULL) {
		return NULL;
	}
	s->step = 0;
	s->iters = 0;
	s->start.tv_sec = 0;
	s->start.tv_nsec = 0;
	s->on_tick = on_tick;
	s->userdata = userdata;

	return s;
}

void
spinner_destroy (struct spinner **s)
{
	if (s == NULL || *s == NULL) {
		return;
	}
	free(*s);
	*s = NULL;
}

void
spinner_repaint (struct spinner *s, cairo_t *cr, int x, int y)
{
	(void)s;
	(void)cr;
	(void)x;
	(void)y;
}
