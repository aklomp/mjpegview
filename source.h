struct source {
	char     *name;
	bool    (*open)(struct source *);
	ssize_t (*read)(struct source *, void *buf, size_t bufsize);
	void    (*close)(struct source *);
	void    (*destroy)(struct source **);
};

bool source_init (
	struct source *,
	const char *const name,
	bool    (*open)(struct source *),
	ssize_t (*read)(struct source *, void *buf, size_t bufsize),
	void    (*close)(struct source *),
	void    (*destroy)(struct source **));

void source_deinit (struct source *);
const char *source_get_name (struct source *const);
