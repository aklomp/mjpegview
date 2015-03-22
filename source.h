struct source {
	char *name;
	bool (*open)(struct source *);
	void (*close)(struct source *);
	void (*destroy)(struct source **);
	int  fd;
	int  selfpipe_readfd;
};

bool source_init (
	struct source *,
	const char *const name,
	bool (*open)(struct source *),
	void (*close)(struct source *),
	void (*destroy)(struct source **));

void source_deinit (struct source *);
void source_set_selfpipe (struct source *, int pipe_read_fd);
ssize_t source_read (struct source *, void *buf, size_t bufsize);
const char *source_get_name (struct source *const);
