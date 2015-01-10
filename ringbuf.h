struct ringbuf;

struct ringbuf *ringbuf_create (unsigned int size, size_t elemsize, void (*on_destroy)(void *));
void ringbuf_destroy (struct ringbuf **);
void ringbuf_append (struct ringbuf *, void *datum);
void *ringbuf_oldest (struct ringbuf *);
void *ringbuf_newest (struct ringbuf *);
unsigned int ringbuf_size (struct ringbuf *);
unsigned int ringbuf_used (struct ringbuf *);
