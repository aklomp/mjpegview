struct mjv_source;
struct mjv_config_source;

enum { TYPE_NETWORK, TYPE_FILE };

struct mjv_source *mjv_source_create_from_file (const char *const, const char *const, const int);
struct mjv_source *mjv_source_create_from_network (const char *const, const char *const, const char *const, const char *const, const char *const, const int);
void mjv_source_destroy (struct mjv_source **const);
int mjv_source_open (struct mjv_source *cs);

int         mjv_source_get_fd   (const struct mjv_source *const);
int         mjv_source_get_port (const struct mjv_source *const);
int         mjv_source_get_usec (const struct mjv_source *const);
int         mjv_source_get_type (const struct mjv_source *const);
const char *mjv_source_get_file (const struct mjv_source *const);
const char *mjv_source_get_name (const struct mjv_source *const);
const char *mjv_source_get_host (const struct mjv_source *const);
const char *mjv_source_get_path (const struct mjv_source *const);
const char *mjv_source_get_user (const struct mjv_source *const);
const char *mjv_source_get_pass (const struct mjv_source *const);
