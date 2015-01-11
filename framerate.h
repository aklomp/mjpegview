struct framerate;

struct framerate *framerate_create (void);
void framerate_destroy (struct framerate **f);
void framerate_insert_datapoint (struct framerate *f, const struct timespec *const ts);
float framerate_estimate (struct framerate *f);
