struct mjv_frame;

struct mjv_frame *mjv_frame_create (const char *const, const unsigned int);
void mjv_frame_destroy (struct mjv_frame *const);
unsigned char *mjv_frame_to_pixbuf (struct mjv_frame *);

unsigned int mjv_frame_get_width (const struct mjv_frame *const frame);
unsigned int mjv_frame_get_height (const struct mjv_frame *const frame);
unsigned int mjv_frame_get_row_stride (const struct mjv_frame *const frame);

struct timespec *mjv_frame_get_timestamp (const struct mjv_frame *const frame);
