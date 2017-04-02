#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

/* If compiling on windows use these */
#ifdef _WIN32
#include <string.h>

static char buffer[2048];

/* fake readline function */
char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);

    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';

    return cpy;
}

void add_history(char* unused) {}

/* Otherwise */
#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

//Recursively counts the total number of nodes in our Abstract Syntax Tree
int numberOfNodes(mpc_ast_t* tree) {
    if(tree->children_num == 0)
        return 1;

    if(tree->children_num >= 1) {
        int total = 1;

        for(int i = 0; i < tree->children_num; i++) {
            total += numberOfNodes(tree->children[i]);
        }

        return total;
    }

    return 0;
}

int main(int argc, char** argv) {
    /* Create some parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Lispy = mpc_new("lispy");

    /* Define the parsers */
    mpca_lang(MPCA_LANG_DEFAULT,
        "                                                       \
            number   :  /-?[0-9]+/ ;                            \
            operator :  '+' | '-' | '*' | '/' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"mod\";                 \
            expr     :  <number> | '(' <operator> <expr>+ ')' ; \
            lispy    :  /^/ <operator> <expr>+ /$/ ;            \
        ",
        Number, Operator, Expr, Lispy
    );

    /* Print Version and Exit info */
    puts("Lispy Version 0.0.0.0.1");
    puts("Press Ctrl+C to Exit\n\n");

    /* Main program loop */
    while(1) {
        /* Output the prompt and get input - using editline for *nix */
        char* input = readline("lispy> ");

        /* Add input to history */
        add_history(input);

        /* Attempt to parse user input */
        mpc_result_t result;

        if(mpc_parse("<stdin>", input, Lispy, &result)) {
            /* Load the Abstract Syntax Tree from output */
            mpc_ast_t* ast = result.output;
            printf("Tag: %s\n", ast->tag);
            printf("Contents: %s\n", ast->contents);
            printf("Number of children: %i\n", ast->children_num);

            printf("Number of nodes: %i\n", numberOfNodes(ast));
        } else {
            /* Otherwise print the error */
            mpc_err_print(result.error);
            mpc_err_delete(result.error);
        }

        /* Free retrieved input */
        free(input);
    }

    /* Clean up the parsers */
    mpc_cleanup(4, Number, Operator, Expr, Lispy);

    return 0;
}
