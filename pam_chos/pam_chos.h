#define DEFAULT_ENV_NAME "default"
#define LINKCHOS "/proc/chos/link"
#define MAXLINE 80
#define MAX_LEN 256
#define RESETCHOS "/proc/chos/resetchos"
#define SETCHOS "/proc/chos/setchos"
#define USER_CONF_FILE_DEFAULT ".chos"

/* Configuration structure for pam_chos */
typedef struct pam_chos_config {
  /*
   * Name of the per-user configuration file, relative to the user's
   * home directory.
   */
  char *user_conf_file;
  /*
   * If enabled, place the user in the default environment if the
   * contents of the per-user configuration file are considered invalid.
   * Otherwise, attempt to use the per-user configuration.
   */
  int fail_to_default;
} pam_chos_config;

int set_multi(char *os);

char * check_chos(char *name);

int read_chos_file(char *user_conf_file, char *dir, char *osenv);

pam_chos_config *init_pam_chos_config(void);

void parse_pam_chos_args(pam_chos_config *args, int argc, const char
  **argv);

int argmatch(const char *arg, const char *match);

int is_valid_char(char c, int is_path);

void sanitize_name(char *s, int length);
void sanitize_path(char *s, int length);
void sanitize_str(char *s, int length, int is_path);
char *read_line_from_fd(FILE *f, char *dest);
