struct mjv_source;
struct mjv_config_source;

struct mjv_source *mjv_source_create (struct mjv_config_source *);
bool mjv_source_open (struct mjv_source *);
void mjv_source_capture (struct mjv_source *);
void mjv_source_destroy (struct mjv_source *);

// getters
unsigned int mjv_source_get_id (const struct mjv_source const *);
const char *mjv_source_get_name (const struct mjv_source const *);

// setters
unsigned int mjv_source_set_name (struct mjv_source *const, const char *const name);
void mjv_source_set_callback (struct mjv_source *s, void (*got_frame_callback)(struct mjv_frame *, void *), void *);
