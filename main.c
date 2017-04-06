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

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

/* Structs */
    /* Set up the basic lisp value struct to handle interpreter output */
    typedef struct lval {
        int type;
        long num;

        //Error and Symbol types store string data
        char* err;
        char* symbol;

        //Count and Pointer to list of lval
        int count;
        struct lval** cell;
    } lval;

    //LVAL types
    enum { LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_ERR };

/* Functions */
    //Forward definers
    void lval_print(lval* val);
    lval* lval_eval_sexpr(lval* val);

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

/* Constructor functions */
    //Create a new number type lval
    lval* lval_num(long x) {
        lval* val = malloc(sizeof(lval));

        val->type = LVAL_NUM;
        val->num = x;

        return val;
    }

    //Create a new symbol type lval
    lval* lval_sym(char* sym) {
        lval* val = malloc(sizeof(lval));

        val->type = LVAL_SYM;
        val->symbol = malloc(strlen(sym) + 1);

        strcpy(val->symbol, sym);

        return val;
    }

    //Create a new s-expr type lval
    lval* lval_sexpr(void) {
        lval* val = malloc(sizeof(lval));

        val->type = LVAL_SEXPR;
        val->count = 0;
        val->cell = NULL;

        return val;
    }

    //Create a new q-expr type lval
    lval* lval_qexpr(void) {
        lval* val = malloc(sizeof(lval));

        val->type = LVAL_QEXPR;
        val->count = 0;
        val->cell = NULL;

        return val;
    }

    //Create a new error type lval
    lval* lval_err(char* msg) {
        lval* val = malloc(sizeof(lval));

        val->type = LVAL_ERR;
        val->err = malloc(strlen(msg) + 1);
        strcpy(val->err, msg);

        return val;
    }

/* LVAL Util Functions */
    //Deletes an lval and frees the allocated memory
    void lval_del(lval* val) {
        switch(val->type) {
            //Nothing special for the number type
            case LVAL_NUM: break;

            //Free the string memory for error or symbol
            case LVAL_ERR: free(val->err); break;
            case LVAL_SYM: free(val->symbol); break;

            //If q-expression or s-expression then delete all elements inside
            case LVAL_QEXPR:
            case LVAL_SEXPR:

                for(int i = 0; i < val->count; i++) {
                    lval_del(val->cell[i]);
                }

                //Also free the memory to contain the pointers
                free(val->cell);

                break;
        }

        //Free the memory allocated to lval itself
        free(val);
    }

    //Adds an lval to the heap
    lval* lval_add(lval* parent, lval* toAdd) {
        parent->count++;
        parent->cell = realloc(parent->cell, sizeof(lval*) * parent->count);
        parent->cell[parent->count - 1] = toAdd;

        return parent;
    }

    //Reads a number type lval
    lval* lval_read_num(mpc_ast_t* tree) {
        errno = 0;
        long x = strtol(tree->contents, NULL, 10);
        return (errno != ERANGE) ? lval_num(x) : lval_err("Invalid Number");
    }

    //Reads an lval
    lval* lval_read(mpc_ast_t* tree) {
        //If symbol or number return conversion to that type
        if(strstr(tree->tag, "number"))
            return lval_read_num(tree);

        if(strstr(tree->tag, "symbol"))
            return lval_sym(tree->contents);

        //If root or sexpr then create empty list
        lval* val = NULL;

        if(strcmp(tree->tag, ">") == 0 || strstr(tree->tag, "sexpr"))
            val = lval_sexpr();

        if(strstr(tree->tag, "qexpr"))
            val = lval_qexpr();

        //Fill the list with any valid expression contained within
        for(int i = 0; i < tree->children_num; i++) {
            if(strcmp(tree->children[i]->contents, "(") == 0) continue;
            if(strcmp(tree->children[i]->contents, ")") == 0) continue;
            if(strcmp(tree->children[i]->contents, "{") == 0) continue;
            if(strcmp(tree->children[i]->contents, "}") == 0) continue;
            if(strcmp(tree->children[i]->tag, "regex") == 0) continue;


            val = lval_add(val, lval_read(tree->children[i]));
        }

        return val;
    }

    //Gets an lval at the specified index leaving the list intact
    lval* lval_pop(lval* val, int index) {
        //Find the item at 'index'
        lval* poppedVal = val->cell[index];

        //Shift the memory after the item at 'index'
        memmove(&val->cell[index], &val->cell[index+1], sizeof(lval*) * (val->count - index - 1));

        //Decrease the count of items in the list
        val->count--;

        //Reallocate the memory used
        val->cell = realloc(val->cell, sizeof(lval*) * val->count);

        return poppedVal;
    }

    //Same as lval_pop, but deletes remaining list
    lval* lval_take(lval* val, int index) {
        lval* takeVal = lval_pop(val, index);
        lval_del(val);

        return takeVal;
    }

    //Evaluate an s-expression
    lval* lval_eval(lval* val) {
        //Evaluate s-expressions
        if(val->type == LVAL_SEXPR)
            return lval_eval_sexpr(val);

        //All other lval types remain the same
        return val;
    }

    //Perform an operation on an expression
    lval* builtin_op(lval* args, char* op) {
        //Ensure all args are numbers
        for(int i = 0; i < args->count; i++) {
            if(args->cell[i]->type != LVAL_NUM) {
                lval_del(args);

                return lval_err("Cannot operate on non-number!");
            }
        }

        //Pop the first element
        lval* x = lval_pop(args, 0);

        //If no arguments and sub then perform unary negation
        if((strcmp(op, "-") == 0) && args->count == 0) {
            x->num = -x->num;
        }

        //While there are still elements remaining
        while(args->count > 0) {
            //Pop the next element
            lval* y = lval_pop(args, 0);

            if(strcmp(op, "+") == 0)
                x->num += y->num;
            else if(strcmp(op, "-") == 0)
                x->num -= y->num;
            else if(strcmp(op, "*") == 0)
                x->num *= y->num;
            else if(strcmp(op, "/") == 0) {
                if(y->num == 0) {
                    lval_del(x);
                    lval_del(y);

                    x = lval_err("Cannot Divide by Zero!");
                    break;
                }

                x->num /= y->num;
            }
            else if(strcmp(op, "%") == 0) {
                if(y->num == 0) {
                    lval_del(x);
                    lval_del(y);

                    x = lval_err("Cannot Divide by Zero!");
                    break;
                }

                x->num %= y->num;
            }
            else if(strcmp(op, "^") == 0)
                x->num = pow(x->num, y->num);

            lval_del(y);
        }

        lval_del(args);

        return x;
    }

    lval* builtin_head(lval* val) {
        //Check error conditions
        LASSERT(val, val->count == 1, "Argument Error: Function 'head' was passed too many arguments.");
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'head' expects type Q-Expression.");
        LASSERT(val, val->cell[0]->count != 0, "Empty Expression: Function 'head' expects at least one value. Passed: {}");

        //Otherwise take first arg
        lval* newVal = lval_take(val, 0);

        //Delete all elements that are not the head and return
        while(newVal->count > 1) {
            lval_del(lval_pop(newVal, 1));
        }

        return newVal;
    }

    lval* builtin_tail(lval* val) {
        //Check error conditions
        LASSERT(val, val->count == 1, "Argument Error: Function 'tail' was passed too many arguments.");
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'tail' expects type Q-Expression.");
        LASSERT(val, val->cell[0]->count != 0, "Empty Expression: Function 'tail' expects at least one value. Passed: {}");

        //Otherwise take first arg
        lval* newVal = lval_take(val, 0);

        //Delete the first and return
        lval_del(lval_pop(newVal, 0));

        return newVal;
    }

    lval* builtin_list(lval* val) {
        val->type = LVAL_QEXPR;
        return val;
    }

    lval* builtin_eval(lval* val) {
        LASSERT(val, val->count == 1, "Argument Error: Function 'eval' passed too many arguments.");
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'eval' expects type Q-Expression.");

        lval* result = lval_take(val, 0);
        result->type = LVAL_SEXPR;

        return lval_eval(result);
    }

    lval* lval_join(lval* exp1, lval* exp2) {
        //For each cell in 'exp2' add it to 'exp1'
        while(exp2->count) {
            exp1 = lval_add(exp1, lval_pop(exp2, 0));
        }

        //Delete the empty 'exp2'
        lval_del(exp2);

        return exp1;
    }

    lval* builtin_join(lval* val) {
        for(int i = 0; i < val->count; i++) {
            LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'join' expects type Q-Expression.");
        }

        lval* result = lval_pop(val, 0);

        while(val->count) {
            result = lval_join(result, lval_pop(val, 0));
        }

        lval_del(val);

        return result;
    }

    lval* builtin(lval* val, char* func) {
        if (strcmp("list", func) == 0) { return builtin_list(val); }
        if (strcmp("head", func) == 0) { return builtin_head(val); }
        if (strcmp("tail", func) == 0) { return builtin_tail(val); }
        if (strcmp("join", func) == 0) { return builtin_join(val); }
        if (strcmp("eval", func) == 0) { return builtin_eval(val); }
        if (strstr("+-/*^", func)) { return builtin_op(val, func); }

        lval_del(val);

        return lval_err("Unknown Function!");
    }

    lval* lval_eval_sexpr(lval* val) {
        //Evaluate children
        for(int i = 0; i < val->count; i++) {
            val->cell[i] = lval_eval(val->cell[i]);
        }

        //Error checking
        for(int i = 0; i < val->count; i++) {
            if(val->cell[i]->type == LVAL_ERR)
                return lval_take(val, i);
        }

        //Empty Expression
        if(val->count == 0)
            return val;

        //Single Expression
        if(val->count == 1)
            return lval_take(val, 0);

        //Ensure first element is symbol
        lval* first = lval_pop(val, 0);

        if(first->type != LVAL_SYM) {
            lval_del(first);
            lval_del(val);
            return lval_err("S-expression does not start with a symbol!");
        }

        //Call builtin with operator
        lval* result = builtin(val, first->symbol);
        lval_del(first);

        return result;
    }

    //Prints an lval's sub-expressions
    void lval_expr_print(lval* val, char open, char close) {
        putchar(open);

        for(int i = 0; i < val->count; i++) {
            //Print val contained within
            lval_print(val->cell[i]);

            //Don't print trailing space if last
            if(i != (val->count - 1)) {
                putchar(' ');
            }
        }

        putchar(close);
    }

    //Prints an lval
    void lval_print(lval* val) {
        switch(val->type) {
            //If lval is type LVAL_NUM, print it and break
            case LVAL_NUM:
                printf("%li", val->num);
                break;

            //If lval is type LVAL_ERR, check it's error type and print it
            case LVAL_ERR:
                printf("Error: %s", val->err);
                break;

            case LVAL_SYM:
                printf("%s", val->symbol);
                break;

            case LVAL_SEXPR:
                lval_expr_print(val, '(', ')');
                break;

            case LVAL_QEXPR:
                lval_expr_print(val, '{', '}');
                break;
        }
    }

    void lval_println(lval* val) {
        lval_print(val);
        putchar('\n');
    }

/* Main */
int main(int argc, char** argv) {
    /* Create some parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Lispy = mpc_new("lispy");

    /* Define the parsers */
    mpca_lang(MPCA_LANG_DEFAULT,
        "                                                         \
            number   :  /-?[0-9]+/ ;                              \
            symbol   :  \"list\" | \"head\" | \"tail\"            \
                     |  \"join\" | \"eval\" | '+' | '-'           \
                     |  '*' | '/' | '%' | '^' ;                   \
            sexpr    :  '(' <expr>* ')' ;                         \
            qexpr    :  '{' <expr>* '}' ;                         \
            expr     :  <number> | <symbol> | <sexpr> | <qexpr> ; \
            lispy    :  /^/ <expr>* /$/ ;                         \
        ",
        Number, Symbol, Sexpr, Qexpr, Expr, Lispy
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
            lval* evalResult = lval_eval(lval_read(result.output));

            /* Print the result */
            lval_println(evalResult);

            lval_del(evalResult);

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
    mpc_cleanup(4, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

    return 0;
}
