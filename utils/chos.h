/*
 * CHOS (c) 2004, The Regents of the University of California, through
 * Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights
 * reserved.
 * 
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at  TTD@lbl.gov referring to "CHOS (LBNL Ref CR-2025)"
 * 
 * NOTICE.  This software was developed under funding from the U.S.
 * Department of Energy.  As such, the U.S. Government has been granted
 * for itself and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce, prepare
 * derivative works, and perform publicly and display publicly.
 * Beginning five (5) years after the date permission to assert
 * copyright is obtained from the U.S. Department of Energy, and subject
 * to any subsequent five (5) year renewals, the U.S. Government is
 * granted for itself and others acting on its behalf a paid-up,
 * nonexclusive, irrevocable, worldwide license in the Software to
 * reproduce, prepare derivative works, distribute copies to the public,
 * perform publicly and display publicly, and to permit others to do so.
 *
 */


#define SETCHOS "/proc/chos/setchos"
#define RESETCHOS "/proc/chos/resetchos"
#define LINKCHOS "/proc/chos/link"
#define CHOSKOVERSION "/proc/chos/version"

#define ENVHEAD "%ENV"
#define SHELLHEAD "%SHELLS"

#define MAX_OS 80

typedef struct chos_env {
    char *name;
    char *path;
    struct chos_env *next;
} chos_env;

typedef struct chos_config {
    chos_env *envs;
    char **env_vars;
} chos_config;

char **set_env(void);
char *check_chos(char *name);
int argmatch(const char *arg, const char *match);
int chos_parse_args(int argc, char **argv);

int configure_chos(void);
int read_chos_file(char *dir,char *osenv,int max);

void chos_debug(char *msg, ...);
void chos_err(char *msg, ...);

/* Functions to manipulate the current CHOS context mapping */
int get_multi(char *os);
int set_multi(char *os);

/* Functions to print usage and version information */
void chos_print_usage(void);
void chos_print_version(void);

chos_env *chos_create_env(char *name);
static const char *chos_root = "/chos/";
static const char *chos_config_prefix = "/etc/chos.d/";
static const int chos_debug_flag = 0;

