#include <stdio.h>

#include "../framerate.c"

static int
test_timespec_diff ()
{
	struct testcase {
		struct timespec new;
		struct timespec old;
		float expect;
	}
	cases[] = {
	  { { .tv_sec = 0, .tv_nsec = 0 }
	  , { .tv_sec = 0, .tv_nsec = 0 }
	  , 0.0f
	  }
	, { { .tv_sec = 20, .tv_nsec = 0 }
	  , { .tv_sec = 10, .tv_nsec = 0 }
	  , 10.0f
	  }
	, { { .tv_sec = 21, .tv_nsec = 100000000 }
	  , { .tv_sec = 20, .tv_nsec = 900000000 }
	  , 0.2f
	  }
	, { { .tv_sec = 25, .tv_nsec = 700000000 }
	  , { .tv_sec = 20, .tv_nsec = 500000000 }
	  , 5.2f
	  }
	};
	int ret = 0;
	float d;
	for (unsigned int i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
		if ((d = timespec_diff(&cases[i].new, &cases[i].old)) != cases[i].expect) {
			printf("FAIL: %s, #%d: expected %f, got %f\n", __func__, i, cases[i].expect, d);
			ret = 1;
		}
	}
	return ret;
}

static int
test_estimate ()
{
	int ret = 0;
	struct framerate *f = framerate_create(5);

	// No frames, estimate negative:
	if (framerate_estimate(f) >= 0.0f) {
		printf("FAIL: %s\n", __func__);
		ret = 1;
	}
	framerate_destroy(&f);
	return ret;
}

int
main ()
{
	int ret = 0;

	ret |= test_timespec_diff();
	ret |= test_estimate();

	return ret;
}
