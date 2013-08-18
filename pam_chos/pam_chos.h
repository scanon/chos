#define DEFAULT_ENV_NAME "default"
#define LINKCHOS "/proc/chos/link"
#define MAXLINE 80
#define MAX_LEN 256
#define RESETCHOS "/proc/chos/resetchos"
#define SETCHOS "/proc/chos/setchos"
#define USER_CONF_FILE_DEFAULT ".chos"
#define PAM_CHOS_MODULE_NAME "pam_chos"

/* Value to return upon pam_chos error */
#define ONERR PAM_SUCCESS

/* Configuration structure for pam_chos */
typedef struct pam_chos_config {
  /*
   * Name of the per-user configuration file, relative to the user's
   * home directory.
   */
  char user_conf_file[MAX_LEN+1];
  /*
   * If enabled, place the user in the default environment if the
   * contents of the per-user configuration file are considered invalid.
   * Otherwise, attempt to use the per-user configuration.
   */
  int fail_to_default;
} pam_chos_config;

int set_multi(char *os);

/*
 * Look up the path for the alias pointed to by name against the
 * system CHOS configuration.  Returns a pointer to a string
 * containing a valid CHOS path, or NULL is the name can not be
 * validated.
 */
char *check_chos(char *name);

/*
 * Reads a the CHOS user configuration file at user_conf_file within
 * dir.  If an OS environment name entry is present, that name is
 * written into the location specified by osenv.  If no entry
 * is present, or the file cannot be opened, then
 * DEFAULT_ENV_NAME is written instead.  Returns the number
 * of bytes written to osenv, including the trailing '\0' character,
 * or -1 on error.
 */
ssize_t read_chos_file(char *user_conf_file, char *dir, char *osenv);

/*
 * Initializes the pam_chos_config structure at the location pointed
 * to by *cfg with the default settings.  Returns a pointer to the
 * destination structure, or NULL if an error occurs.
 */
pam_chos_config *init_pam_chos_config(pam_chos_config *cfg);

/*
 * Parse argc number of arguments at argv, and update the
 * pam_chos_config structure at cfg accordingly.  Returns 1 on
 * success.  Any return value other than 1 indicates an error
 * occurred.
 */
int parse_pam_chos_args(pam_chos_config *cfg, int argc, const char
  **argv);

/*
 * See if the name at *arg is a match for *match, and that *arg
 * contains a valid value for the option.  Returns 1 on success and
 * some other value on error.
 */
int argmatch(const char *arg, const char *match);

/*
 * A family of functions to sanitize CHOS environment names and
 * paths.
 */
int sanitize_name(char *s, int length);
int sanitize_path(char *s, int length);
int sanitize_str(char *s, int length, int is_path);
int is_valid_char(char c, int is_path);

/* Read a line of up to MAXLINE from an open FILE at *f */
/* Sufficient storage must be available at *dest */
char *read_line_from_file(FILE *f, char *dest);

/*
 * Retrieve the appropriate environment name and path using the $CHOS
 * environment variable, user configuration file, and system
 * configuration file, as specified by the options in **argv.  Write
 * the environment name and path to fd.  Returns 1 on success, or any
 * other value on failure.
 */
int get_chos_info(int argc, const char **argv, int fd, char *user_conf_dir);

/*
 * Retrieve values for env_path and osenv from the file descriptor at
 * fd.  Returns 1 on success, or any other value on failure.
 */
int retrieve_from_child(char *env_path, char *osenv, int fd);

