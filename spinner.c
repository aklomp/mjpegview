// Unlock clock_nanosleep() and friends:
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <cairo.h>

#define STEPS 12
#define TWO_PI        6.28318530717958647692
#define RLARGE        18.0
#define RSMALL        3.0
#define ITERS_PER_SEC  (STEPS)
#define INTERVAL_NSEC  (1000000000 / ITERS_PER_SEC)

struct spinner
{
	void (*on_tick)(void *);
	void *userdata;
	unsigned int step;
	unsigned int iters;
	unsigned int quit;
	struct timespec start;
	pthread_t pthread;
};

static void
sighandler (int sig)
{
	(void)sig;
}

static void *
thread_main (void *userdata)
{
	struct spinner *s = userdata;
	struct timespec now, wake;

	// This function runs the spinner thread.
	// Every interval, update spinner step and issue a tick callback.
	// Continue till canceled.

	// Handle SIGUSR1 (our quit signal) with a stub function:
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGUSR1, &sa, NULL);

	while (!s->quit)
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
			// Do not restart on EINTR, allow sleep to be interrupted:
			clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wake, NULL);
		}
		// Check flag in case sleep was interrupted by spinner_destroy():
		if (s->quit) {
			break;
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
	s->quit = 0;
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
	// Tell thread that it's time to quit:
	(*s)->quit = 1;

	// Signal spinner thread to quit:
	pthread_kill((*s)->pthread, SIGUSR1);

	// Wait for thread to exit:
	pthread_join((*s)->pthread, NULL);
	free(*s);
	*s = NULL;
}

static float sin[] = { 0.0, RLARGE * 0.5, RLARGE * 0.86602540378443864676, RLARGE };

static inline void
step_rgba (int n, struct spinner *s, cairo_t *cr)
{
	cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.9 - 0.06 * ((n + s->step) % STEPS));
}

static inline void
step_paint (cairo_t *cr)
{
	cairo_close_path(cr);
	cairo_fill(cr);
}

void
spinner_repaint (struct spinner *s, cairo_t *cr, int x, int y)
{
	if (s == NULL || cr == NULL) {
		return;
	}
	step_rgba( 0, s, cr); cairo_arc(cr, x + sin[0], y + sin[3], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 1, s, cr); cairo_arc(cr, x + sin[1], y + sin[2], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 2, s, cr); cairo_arc(cr, x + sin[2], y + sin[1], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 3, s, cr); cairo_arc(cr, x + sin[3], y + sin[0], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 4, s, cr); cairo_arc(cr, x + sin[2], y - sin[1], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 5, s, cr); cairo_arc(cr, x + sin[1], y - sin[2], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 6, s, cr); cairo_arc(cr, x + sin[0], y - sin[3], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 7, s, cr); cairo_arc(cr, x - sin[1], y - sin[2], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 8, s, cr); cairo_arc(cr, x - sin[2], y - sin[1], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba( 9, s, cr); cairo_arc(cr, x - sin[3], y - sin[0], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba(10, s, cr); cairo_arc(cr, x - sin[2], y + sin[1], RSMALL, 0, TWO_PI); step_paint(cr);
	step_rgba(11, s, cr); cairo_arc(cr, x - sin[1], y + sin[2], RSMALL, 0, TWO_PI); step_paint(cr);
}
