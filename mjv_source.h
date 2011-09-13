struct mjv_source;

struct mjv_source *mjv_source_create_from_file (const char *, void (*got_frame_callback)(struct mjv_source *, struct mjv_frame *));
struct mjv_source *mjv_source_create_from_network (const char *, const unsigned int, const char *, const char *, const char *, void (*got_frame_callback)(struct mjv_source *, struct mjv_frame *));
void mjv_source_capture (struct mjv_source *);
void mjv_source_destroy (struct mjv_source *);
unsigned int mjv_source_get_id (struct mjv_source *);
