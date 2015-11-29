#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "ast.h"
#include "libparser_a-parser.h"

#include <stdio.h>
#include <stdlib.h>

extern FILE *yyin;
extern AST_NODE *prog;

int main (int argc, char **argv)
{
    if (argc != 2) {
        fputs("usage: parser [source file]\n", stderr);
        exit(1);
    }
    yyin = fopen(argv[1],"r");
    if (yyin == NULL) {
        fputs("Error opening source file.\n", stderr);
        exit(1);
    }
    yyparse();
    printGV(prog, NULL);
    return 0;
}

// vim: set sw=4 ts=4 sts=4 et:
