struct mjv_framebuf;

struct mjv_framebuf *mjv_framebuf_create (unsigned int);
void mjv_framebuf_destroy (struct mjv_framebuf *);
bool mjv_framebuf_append (struct mjv_framebuf *, struct mjv_frame *);
char *mjv_framebuf_status_string (const struct mjv_framebuf *const);
