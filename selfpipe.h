bool selfpipe_pair (int *restrict read_fd, int *restrict write_fd);
void selfpipe_write_close (int *write_fd);
void selfpipe_read_close (int *read_fd);
