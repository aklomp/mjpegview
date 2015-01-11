#include <stdlib.h>
#include <time.h>

#include "ringbuf.h"

struct framerate
{
	struct ringbuf *rb;
};

struct framerate *
framerate_create (unsigned int size)
{
	struct framerate *f;

	if ((f = malloc(sizeof(*f))) == NULL) {
		return NULL;
	}
	if ((f->rb = ringbuf_create(size, sizeof(struct timespec), NULL)) == NULL) {
		free(f);
		return NULL;
	}
	return f;
}

void
framerate_destroy (struct framerate **f)
{
	if (f == NULL || *f == NULL) {
		return;
	}
	ringbuf_destroy(&(*f)->rb);
	free(*f);
	*f = NULL;
}

void
framerate_insert_datapoint (struct framerate *f, const struct timespec *const ts)
{
	ringbuf_append(f->rb, (void *)ts);
}

static float
timespec_diff (struct timespec *new, struct timespec *old)
{
	time_t diff_sec = new->tv_sec - old->tv_sec;
	long diff_nsec = new->tv_nsec - old->tv_nsec;
	return diff_sec + ((float)diff_nsec / 1000000000.0);
}

float
framerate_estimate (struct framerate *f)
{
	struct timespec now;

	// Returns estimate of frames-per-second over the past number of
	// inserted datapoints. Negative values indicate an error or an
	// apparent stall condition.

	// Not enough data for comparison?
	if (f == NULL || ringbuf_used(f->rb) <= 1) {
		return -1.0;
	}
	// Compare oldest and newest values:
	float diff_among_frames = timespec_diff(ringbuf_newest(f->rb), ringbuf_oldest(f->rb));

	// Get the wall time:
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		return (ringbuf_used(f->rb) - 1) / diff_among_frames;
	}
	// Calculate the difference between the last seen frame
	// and the wall time:
	float diff_with_now = timespec_diff(&now, ringbuf_newest(f->rb));

	// If this difference is smaller than the average FPS of
	// the frames among themselves, return the diff among frames:
	// If we have 5 frames, we have 4 intervals; hence ringbuf_used - 1
	if (diff_with_now < diff_among_frames) {
		return (ringbuf_used(f->rb) - 1) / diff_among_frames;
	}
	// Else there has been a large time gap between the last received
	// frame and the now (the connection has lagged). If this is less
	// than 5 times the normal interval, we recalculate the FPS against
	// the current wall time, else return invalid:
	if (diff_with_now > diff_among_frames * 5.0) {
		return -1.0;
	}
	diff_with_now = timespec_diff(&now, ringbuf_oldest(f->rb));
	return ringbuf_used(f->rb) / diff_with_now;
}
