#ifndef MJV_FRAMERATE_H
#define MJV_FRAMERATE_H

struct mjv_framerate;

struct mjv_framerate *mjv_framerate_create (void);
void mjv_framerate_destroy (struct mjv_framerate **f);
void mjv_framerate_insert_datapoint (struct mjv_framerate *f, const struct timespec *const ts);
float mjv_framerate_estimate (struct mjv_framerate *f);

#endif	/* MJV_FRAMERATE_H */
