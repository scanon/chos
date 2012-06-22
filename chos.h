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

#define MAXLINE 80
#define MAX_OS 80
#define ENVHEAD "%ENV"
#define	CONFIG	".chos"
#define MAX_LEN 256

int set_multi();
char *check_chos(char *name);
char **set_env();
int read_chos_file(char *dir,char *osenv,int max);
void chos_err(char *msg, ...);


typedef struct chos_dir {
    char *src;
    char *dest;
    struct chos_dir *next;
} chos_dir;

typedef struct chos_env {
    char *name;
    char *config_file;
    chos_dir *dirs;
    struct chos_env *next;
} chos_env;

chos_env *create_chos_env(char *name);
chos_dir *create_chos_dir(char *src, char *dest);
chos_env *chos_get_env(char *env_name);
void chos_append_dir(chos_dir *dir, char *src, char *dest);
int chos_append_env(char *name);
int chos_populate_dirs(chos_env *env);

static const char *chos_root = "/chos2/";
static const char *chos_config_prefix = "/etc/chos.d/";
static const int chos_debug_flag = 0;

