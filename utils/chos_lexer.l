%{
#include <stdio.h>
#include "chos_parser.h"

extern char *yylval;
extern int line;
%}

%%
[0-9A-Za-z_\-/]+    { yylval=strdup(yytext); return NAME; }
%ENV                return ENVHEADER;
%SHELLS             return SHELLHEADER;
:                   return DELIM;
#.*                 ;
\n                  line++;
[ \t]+              ;
.                   { yyerror("Invalid character"); }
%%
