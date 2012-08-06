struct mjv_framebuf;

struct mjv_framebuf *mjv_framebuf_create (guint);
void mjv_framebuf_destroy (struct mjv_framebuf *);
bool mjv_framebuf_append (struct mjv_framebuf *, struct mjv_frame *);
GString *mjv_framebuf_status_string (const struct mjv_framebuf *const);
