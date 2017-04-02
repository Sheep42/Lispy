#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

/* Structs */
    /* Set up the basic lisp value struct to handle interpreter output */
    typedef struct {
        int type;
        double num;
        int err;
    } lval;

    //LVAL types
    enum { LVAL_NUM, LVAL_ERR };

    //LVAL Error Types
    enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* Functions */
    //Recursively counts the total number of nodes in our Abstract Syntax Tree
    int number_of_nodes(mpc_ast_t* tree) {
        if(tree->children_num == 0)
            return 1;

        if(tree->children_num >= 1) {
            int total = 1;

            for(int i = 0; i < tree->children_num; i++) {
                total += number_of_nodes(tree->children[i]);
            }

            return total;
        }

        return 0;
    }

    //Create a new number type lval
    lval lval_num(long x) {
        lval val;

        val.type = LVAL_NUM;
        val.num = x;

        return val;
    }

    //Create a new error type lval
    lval lval_err(int x) {
        lval val;

        val.type = LVAL_ERR;
        val.err = x;

        return val;
    }

    //Evaluates an operation
    lval eval_op(lval x, char* op, lval y) {
        //If either input is an error return it
        if(x.type == LVAL_ERR) return x;
        if(y.type == LVAL_ERR) return y;

        //Else evaluate the operations
        if(strcmp(op, "+") == 0 || strcmp(op, "add") == 0) return lval_num(x.num + y.num);
        if(strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) return lval_num(x.num - y.num);
        if(strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) return lval_num(x.num * y.num);
        if(strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
            return (y.num == 0) ? lval_err(LERR_DIV_ZERO) : lval_num(x.num / y.num);
        }
        if(strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) return lval_num(fmod(x.num, y.num));
        if(strcmp(op, "^") == 0 || strcmp(op, "pow") == 0) return lval_num(pow(x.num, y.num));

        return lval_err(LERR_BAD_OP);
    }

    //Evaluates an expression
    lval eval(mpc_ast_t* tree) {
        /* If tagged as number return it directly */
        if(strstr(tree->tag, "number")) {
            //Check conversion for errors
            errno = 0;
            double x = strtold(tree->contents, NULL);
            return (errno != ERANGE) ? lval_num(x) : lval_err(LERR_BAD_NUM);
        }

        /* The operator is always second child */
        char* op = tree->children[1]->contents;

        /* Store the third child in 'x' */
        lval x = eval(tree->children[2]);

        /* Iterate the remaining children and combine */
        int i = 3;
        while(strstr(tree->children[i]->tag, "expr")) {
            x = eval_op(x, op, eval(tree->children[i]));
            i++;
        }

        return x;
    }

    void lval_print(lval val) {
        switch(val.type) {
            //If lval is type LVAL_NUM, print it and break
            case LVAL_NUM:
                printf("%.2f", roundf(val.num));
                break;

            //If lval is type LVAL_ERR, check it's error type and print it
            case LVAL_ERR:
                if(val.err == LERR_DIV_ZERO)
                    printf("Error: Divide by Zero");
                else if(val.err == LERR_BAD_OP)
                    printf("Error: Invalid operation");
                else if(val.err == LERR_BAD_NUM)
                    printf("Errpr: Invalid number");

                break;
        }
    }

    void lval_println(lval val) {
        lval_print(val);
        putchar('\n');
    }

/* Main */
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
            operator :  '+' | '-' | '*' | '/' | '%' | '^' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"mod\" | \"pow\";                 \
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
            /* Evaluate the Abstract Syntax Tree from output */
            lval evalResult = eval(result.output);

            /* Print the result */
            lval_println(evalResult);

            /* Delete the result when we are done */
            mpc_ast_delete(result.output);
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
