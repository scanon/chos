%{
#include <stdio.h>
#include <string.h>
#include "chos.h"
#define YYSTYPE char *
 
int line=1;

void yyerror(const char *str) {
        fprintf(stderr,"Parse error on line %d: %s\n",line,str);
}
 
int yywrap() {
        return 1;
} 
%}


%token DELIM
%token ENVHEADER
%token NAME
%token SHELLHEADER


%%

sections:                   /* \0 */
                            | sections section
                            ;

section:                    SHELLHEADER shell_config_entries
                            | ENVHEADER env_config_entries
                            ;

shell_config_entries:       /* \0 */
                            |
                            shell_config_entries shell_config_entry
                            ; 

shell_config_entry:         NAME DELIM NAME
                            {
                                chos_append_env($1, $3, "");
                            }
                            ;

env_config_entries:         /* \0 */
                            |
                            env_config_entries env_config_entry
                            ; 

env_config_entry:           NAME
                            {
                                chos_append_env_var($1);
                            }
                            ;
