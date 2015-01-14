struct ringbuf;

struct ringbuf *ringbuf_create (unsigned int size, size_t elemsize, void (*on_destroy)(void *));
void ringbuf_destroy (struct ringbuf **);
void ringbuf_append (struct ringbuf *, const void *datum);
void *ringbuf_oldest (const struct ringbuf *);
void *ringbuf_newest (const struct ringbuf *);
unsigned int ringbuf_size (const struct ringbuf *);
unsigned int ringbuf_used (const struct ringbuf *);
