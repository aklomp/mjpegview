struct mjv_config;
struct mjv_config_source;

enum { TYPE_NETWORK, TYPE_FILE };

struct mjv_config *mjv_config_init (void);
void mjv_config_destroy( struct mjv_config *const);
bool mjv_config_read_file( struct mjv_config *const, const char *const);
struct mjv_config_source *mjv_config_source_create_from_file (const char *const, const char *const, const int);
struct mjv_config_source *mjv_config_source_create_from_network (const char *const, const char *const, const char *const, const char *const, const char *const, const int);
void mjv_config_source_destroy (struct mjv_config_source *);
const GList const* mjv_config_get_sources (struct mjv_config *const);

// Getters
int mjv_config_source_get_port (const struct mjv_config_source *const);
int mjv_config_source_get_usec (const struct mjv_config_source *const);
int mjv_config_source_get_type (const struct mjv_config_source *const);
const char *mjv_config_source_get_file (const struct mjv_config_source *const);
const char *mjv_config_source_get_name (const struct mjv_config_source *const);
const char *mjv_config_source_get_host (const struct mjv_config_source *const);
const char *mjv_config_source_get_path (const struct mjv_config_source *const);
const char *mjv_config_source_get_user (const struct mjv_config_source *const);
const char *mjv_config_source_get_pass (const struct mjv_config_source *const);
