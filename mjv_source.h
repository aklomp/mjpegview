struct mjv_source;

struct mjv_source *mjv_source_create_from_file (const char *, unsigned int);
struct mjv_source *mjv_source_create_from_network (const char *, const unsigned int, const char *, const char *, const char *);
void mjv_source_capture (struct mjv_source *);
void mjv_source_destroy (struct mjv_source *);

// getters
unsigned int mjv_source_get_id (const struct mjv_source const *);
const char *mjv_source_get_name (const struct mjv_source const *);

// setters
unsigned int mjv_source_set_name (struct mjv_source *const, const char *const name);
void mjv_source_set_callback (struct mjv_source *s, void (*got_frame_callback)(struct mjv_frame *, void *), void *);
