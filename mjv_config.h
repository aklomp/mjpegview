struct mjv_config;

struct mjv_config *mjv_config_init (void);
void mjv_config_destroy (struct mjv_config *);
bool mjv_config_read_file (struct mjv_config *, const char *);
