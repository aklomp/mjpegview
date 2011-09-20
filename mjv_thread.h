struct mjv_thread;

struct mjv_thread *mjv_thread_create (struct mjv_source *);
void mjv_thread_destroy (struct mjv_thread *);
bool mjv_thread_run (struct mjv_thread *);
bool mjv_thread_cancel (struct mjv_thread *);

// Getters
unsigned int mjv_thread_get_height (struct mjv_thread *);
unsigned int mjv_thread_get_width (struct mjv_thread *);
const GtkWidget *mjv_thread_get_canvas (struct mjv_thread *);
