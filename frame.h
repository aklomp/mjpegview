struct frame;

struct frame *frame_create (const char *const, const unsigned int);
void frame_destroy (struct frame **const);
unsigned char *frame_to_pixbuf (struct frame *);

unsigned int frame_get_width (const struct frame *const frame);
unsigned int frame_get_height (const struct frame *const frame);
unsigned int frame_get_row_stride (const struct frame *const frame);

unsigned char *frame_get_rawbits (const struct frame *const frame);
unsigned int frame_get_num_rawbits (const struct frame *const frame);

struct timespec *frame_get_timestamp (const struct frame *const frame);
