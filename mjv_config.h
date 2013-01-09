struct mjv_config;

struct mjv_config *mjv_config_init (void);
struct mjv_config *mjv_config_create_from_file (const char *const filename);
void mjv_config_destroy (struct mjv_config **const);
bool mjv_config_read_file (struct mjv_config *const, const char *const);
bool mjv_config_get_sources (struct mjv_config *const);

struct mjv_source *mjv_config_source_first (struct mjv_config *c);
struct mjv_source *mjv_config_source_next (struct mjv_config *c);
