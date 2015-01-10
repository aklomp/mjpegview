struct framebuf;

struct framebuf *framebuf_create (unsigned int);
void framebuf_destroy (struct framebuf *);
void framebuf_append (struct framebuf *, struct mjv_frame *);
char *framebuf_status_string (const struct framebuf *const);
