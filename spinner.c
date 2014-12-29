// Unlock clock_nanosleep() and friends:
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <cairo.h>

#define STEPS 12
#define ITERS_PER_SEC  (STEPS)
#define INTERVAL_NSEC  (1000000000 / ITERS_PER_SEC)

struct spinner
{
	void (*on_tick)(void *);
	void *userdata;
	unsigned int step;
	unsigned int iters;
	struct timespec start;
	pthread_t pthread;
};

static void *
thread_main (void *userdata)
{
	struct spinner *s = userdata;
	struct timespec now, wake;

	// This function runs the spinner thread.
	// Every interval, update spinner step and issue a tick callback.
	// Continue till canceled.
	for (;;)
	{
		// Get absolute time, calculated from the start time and the number
		// of iterations, of when the next tick should be issued. Aligning
		// the timing to an absolute clock prevents framerate drift.
		wake.tv_sec  = s->start.tv_sec + s->iters / ITERS_PER_SEC;
		wake.tv_nsec = s->start.tv_nsec + (s->iters % ITERS_PER_SEC) * INTERVAL_NSEC;
		if (wake.tv_nsec >= 1000000000) {
			wake.tv_sec++;
			wake.tv_nsec -= 1000000000;
		}
		// `wake' holds the time that we should wake up at. If this time is
		// already in the past, we are out of sync and rebase time to `now':
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > wake.tv_sec || (now.tv_sec == wake.tv_sec && now.tv_nsec >= wake.tv_nsec)) {
			s->iters = 0;
			memcpy(&s->start, &now, sizeof(struct timespec));
		}
		else {
			while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wake, NULL) != 0);
		}
		// Issue a tick callback:
		s->on_tick(s->userdata);

		// Continue:
		s->step = (s->step + 1) % STEPS;
		s->iters++;
	}
	return NULL;
}

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

	// Start the spinner pthread:
	if (pthread_create(&s->pthread, NULL, thread_main, s) != 0) {
		free(s);
		return NULL;
	}
	return s;
}

void
spinner_destroy (struct spinner **s)
{
	if (s == NULL || *s == NULL) {
		return;
	}
	// Destroy spinner pthread:
	pthread_cancel((*s)->pthread);
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
