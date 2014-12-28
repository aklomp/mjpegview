struct spinner;

/* Create spinner object.
 *
 * The on_tick() callback is called when the spinner's timer elapses.
 * The owner must call spinner_repaint() with valid cairo context and
 * center coordinates.
 */
struct spinner *spinner_create(void (*on_tick)(void *userdata), void *userdata);

/* Destroy spinner object.
 */
void spinner_destroy(struct spinner **);

/* Repaint the spinner.
 *
 * Owner passes in the cairo context and the center coordinates.
 * This works synchronously; when the function returns, the repaint
 * is done.
 */
void spinner_repaint(struct spinner *, cairo_t *, int x, int y);
