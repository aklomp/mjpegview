#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FRAMERATE_MEMORY  15

struct mjv_framerate
{
	int num;
	struct timespec mem[FRAMERATE_MEMORY];
};

struct mjv_framerate *
mjv_framerate_create (void)
{
	struct mjv_framerate *f;

	if ((f = malloc(sizeof(*f))) == NULL) {
		return NULL;
	}
	f->num = 0;
	return f;
}

void
mjv_framerate_destroy (struct mjv_framerate **f)
{
	if (f == NULL || *f == NULL) {
		return;
	}
	free(*f);
	*f = NULL;
}

void
mjv_framerate_insert_datapoint (struct mjv_framerate *f, const struct timespec *const ts)
{
	// Shift existing timestamps one over:
	memmove(&f->mem[1], &f->mem[0], sizeof(struct timespec) * (FRAMERATE_MEMORY - 1));

	// Add new value at start:
	memcpy(&f->mem[0], ts, sizeof(*ts));

	// Adjust total number of timestamps in history buffer:
	if (f->num < FRAMERATE_MEMORY) {
		f->num++;
	}
}

static float
timespec_diff (struct timespec *new, struct timespec *old)
{
	time_t diff_sec = new->tv_sec - old->tv_sec;
	long diff_nsec = new->tv_nsec - old->tv_nsec;
	return diff_sec + ((float)diff_nsec / 1000000000.0);
}

static inline struct timespec *
newest (struct mjv_framerate *f)
{
	// Pointer to newest timestamp:
	return &f->mem[0];
}

static inline struct timespec *
oldest (struct mjv_framerate *f)
{
	// Pointer to oldest timestamp:
	return &f->mem[f->num - 1];
}

float
mjv_framerate_estimate (struct mjv_framerate *f)
{
	struct timespec now;

	// Returns estimate of frames-per-second over the past number of
	// inserted datapoints. Negative values indicate an error or an
	// apparent stall condition.

	// Not enough data for comparison?
	if (f == NULL || f->num <= 1) {
		return -1.0;
	}
	// Compare oldest and newest values:
	float diff_among_frames = timespec_diff(newest(f), oldest(f));

	// Get the wall time:
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		return (f->num - 1) / diff_among_frames;
	}
	// Calculate the difference between the last seen frame
	// and the wall time:
	float diff_with_now = timespec_diff(&now, newest(f));

	// If this difference is smaller than the average FPS of
	// the frames among themselves, return the diff among frames:
	// If we have 5 frames, we have 4 intervals; hence f->num - 1
	if (diff_with_now < diff_among_frames) {
		return (f->num - 1) / diff_among_frames;
	}
	// Else there has been a large time gap between the last received
	// frame and the now (the connection has lagged). If this is less
	// than 5 times the normal interval, we recalculate the FPS against
	// the current wall time, else return invalid:
	if (diff_with_now > diff_among_frames * 5.0) {
		return -1.0;
	}
	diff_with_now = timespec_diff(&now, oldest(f));
	return f->num / diff_with_now;
}
