struct mjv_config;
struct mjv_config_source;

struct mjv_config *mjv_config_init (void);
void mjv_config_destroy (struct mjv_config *);
bool mjv_config_read_file (struct mjv_config *, const char *);
const GList const* mjv_config_get_sources (struct mjv_config *);

// Getters
      int   mjv_config_source_get_port (struct mjv_config_source *);
const char *mjv_config_source_get_name (struct mjv_config_source *);
const char *mjv_config_source_get_host (struct mjv_config_source *);
const char *mjv_config_source_get_path (struct mjv_config_source *);
const char *mjv_config_source_get_user (struct mjv_config_source *);
const char *mjv_config_source_get_pass (struct mjv_config_source *);
