#ifndef MJV_GRABBER_H
#define MJV_GRABBER_H

struct mjv_grabber;

// Return codes for mjv_grabber_run:
enum mjv_grabber_status
{ MJV_GRABBER_SUCCESS
, MJV_GRABBER_TIMEOUT
, MJV_GRABBER_READ_ERROR
, MJV_GRABBER_PREMATURE_EOF
, MJV_GRABBER_CORRUPT_HEADER
};

struct mjv_grabber *mjv_grabber_create();
void mjv_grabber_destroy (struct mjv_grabber**);

// The main function. This grabs frames from the source and relays them
// to a callback function:
enum mjv_grabber_status mjv_grabber_run (struct mjv_grabber*);
void mjv_grabber_set_callback (struct mjv_grabber *s, void (*got_frame_callback)(struct mjv_frame*, void*), void*);
void mjv_grabber_set_selfpipe (struct mjv_grabber *s, int pipe_read_fd);

#endif	// MJV_GRABBER_H
