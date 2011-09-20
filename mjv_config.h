struct mjv_config;
struct mjv_config_source;

enum { TYPE_NETWORK, TYPE_FILE };

struct mjv_config *mjv_config_init (void);
void mjv_config_destroy (struct mjv_config *);
bool mjv_config_read_file (struct mjv_config *, const char *);
struct mjv_config_source *mjv_config_source_create_from_file (const char *, const char *, int );
struct mjv_config_source *mjv_config_source_create_from_network (const char *, const char *, const char *, const char *, const char *, int);
void mjv_config_source_destroy (struct mjv_config_source *);
const GList const* mjv_config_get_sources (struct mjv_config *);

// Getters
int mjv_config_source_get_port (struct mjv_config_source *);
int mjv_config_source_get_usec (struct mjv_config_source *);
int mjv_config_source_get_type (struct mjv_config_source *);
const char *mjv_config_source_get_file (struct mjv_config_source *);
const char *mjv_config_source_get_name (struct mjv_config_source *);
const char *mjv_config_source_get_host (struct mjv_config_source *);
const char *mjv_config_source_get_path (struct mjv_config_source *);
const char *mjv_config_source_get_user (struct mjv_config_source *);
const char *mjv_config_source_get_pass (struct mjv_config_source *);
