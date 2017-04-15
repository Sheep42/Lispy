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

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval_del(args); \
        return lval_err(fmt, ##__VA_ARGS__); \
    }

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i. Got %s, Expected %s.", \
    func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
  LASSERT(args, args->count == num, \
    "Function '%s' passed incorrect number of arguments. Got %i, Expected %i.", \
    func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
  LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed {} for argument %i.", func, index);

/* Forward definers */
    struct lval;
    struct lenv;
    typedef struct lval lval;
    typedef struct lenv lenv;

    typedef lval*(*lbuiltin)(lenv*, lval*);
    void lval_print(lval* val);
    void lval_del(lval* val);
    lval* lval_cpy(lval* vals);
    lval* lval_err(char* fmt, ...);
    lval* lval_eval_sexpr(lenv* env, lval* val);
    char* ltype_name(int type);

/* Structs & Function Pointers */
    /* Set up the basic lisp value struct to handle interpreter output */
    typedef struct lval {
        int type;

        /* Basic */
        long num;

        //Error and Symbol types store string data
        char* err;
        char* symbol;

        /* Function */
        lbuiltin builtin;
        lenv* env;
        lval* formals;
        lval* body;

        /* Expression */
        int count;
        struct lval** cell;
    } lval;

    struct lenv {
        lenv* parent;
        int count;
        char** symbols;
        lval** vals;
    };

    //LVAL types

    enum { LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_ERR, LVAL_FUN };

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

/* Constructor functions */
    //Create a new environment
    lenv* lenv_new(void) {
        lenv* env = malloc(sizeof(lenv));

        env->parent = NULL;
        env->count = 0;
        env->symbols = NULL;
        env->vals = NULL;

        return env;
    }

    void lenv_del(lenv* env) {
        for(int i = 0; i < env->count; i++) {
            free(env->symbols[i]);
            lval_del(env->vals[i]);
        }

        free(env->symbols);
        free(env->vals);
        free(env);
    }

    lval* lenv_get(lenv* env, lval* val) {
        //Iterate over all items in env
        for(int i = 0; i < env->count; i++) {
            //Check if the stored string matches the symbol string
            //If it does, return a copy of the value
            if(strcmp(env->symbols[i], val->symbol) == 0) {
                return lval_cpy(env->vals[i]);
            }
        }

        //If no symbol found check parent/return err
        if(env->parent) {
            return lenv_get(env->parent, val);
        } else {
            return lval_err("Unbound Symbol: '%s'", val->symbol);
        }
    }

    void lenv_set(lenv* env, lval* k, lval* v) {
        //Iterate over all items in env to check if variable exists
        for(int i = 0; i < env->count; i++) {
            //If var is found delete item at that pos and replace
            if(strcmp(env->symbols[i], k->symbol) == 0) {
                lval_del(env->vals[i]);
                env->vals[i] = lval_cpy(v);
                return;
            }
        }

        //If no existing entry found, allocate space for new entry
        env->count++;
        env->vals = realloc(env->vals, sizeof(lval*) * env->count);
        env->symbols = realloc(env->symbols, sizeof(char*) * env->count);

        //Copy contents of lval and symbol string into new location
        env->vals[env->count - 1] = lval_cpy(v);
        env->symbols[env->count - 1] = malloc(strlen(k->symbol));
        strcpy(env->symbols[env->count - 1], k->symbol);
    }

    //Copies an environment
    lenv* lenv_cpy(lenv* env) {
        lenv* cpy = malloc(sizeof(lenv));

        cpy->parent = env->parent;
        cpy->count = env->count;
        cpy->symbols = malloc(sizeof(char*) * cpy->count);
        cpy->vals = malloc(sizeof(lval*) * cpy->count);

        for(int i = 0; i < env->count; i++) {
            cpy->symbols[i] = malloc(strlen(env->symbols[i]) + 1);
            strcpy(cpy->symbols[i], env->symbols[i]);
            cpy->vals[i] = lval_cpy(env->vals[i]);
        }

        return cpy;
    }

    //Defines a variable in the global env
    void lenv_def(lenv* env, lval* key, lval* val) {
        //Iterate til env has no parent
        while(env->parent) {
            env = env->parent;
        }

        //Put value in e
        lenv_set(env, key, val);
    }

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

    //Create a new function type lval
    lval* lval_fun(lbuiltin func) {
        lval* vals = malloc(sizeof(lval));

        vals->type = LVAL_FUN;
        vals->builtin = func;

        return vals;
    }

    lval* lval_lambda(lval* formals, lval* body) {
        lval* result = malloc(sizeof(lval));

        result->type = LVAL_FUN;

        //Set builtin to NULL
        result->builtin = NULL;

        //Build new environment
        result->env = lenv_new();

        //Set formals and body
        result->formals = formals;
        result->body = body;

        return result;
    }

    //Create a new error type lval
    lval* lval_err(char* fmt, ...) {

        lval* val = malloc(sizeof(lval));
        val->type = LVAL_ERR;

        //Create va list and initialize it
        va_list va;
        va_start(va, fmt);

        val->err = malloc(512);

        //printf the error with the string w/ max of 511 chars
        vsnprintf(val->err, 511, fmt, va);

        //Reallocate to numer of bytes actually used
        val->err = realloc(val->err, strlen(val->err) + 1);

        //clean up the va list
        va_end(va);

        return val;
    }

/* LVAL Util Functions */
    //Deletes an lval and frees the allocated memory
    void lval_del(lval* val) {
        switch(val->type) {
            //Nothing special for the number or func type
            case LVAL_FUN:
                if(!val->builtin) {
                    lenv_del(val->env);
                    lval_del(val->formals);
                    lval_del(val->body);
                }
                break;

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

    //Evaluate an lval
    lval* lval_eval(lenv* env, lval* val) {
        //Evaluate symbols
        if(val->type == LVAL_SYM) {
            lval* res = lenv_get(env, val);
            lval_del(val);

            return res;
        }

        //Evaluate s-expressions
        if(val->type == LVAL_SEXPR)
            return lval_eval_sexpr(env, val);

        //All other lval types remain the same
        return val;
    }

    //Perform an operation on an expression
    lval* builtin_op(lenv* env, lval* args, char* op) {
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

    lval* builtin_head(lenv* env, lval* val) {
        //Check error conditions
        LASSERT(val, val->count == 1, "Argument Error: Function 'head' was passed too many arguments. Got: %i Expected: %i", val->count, 1);
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'head' expects type Q-Expression. Got: %s Expected: %s", ltype_name(val->cell[0]->type), ltype_name(LVAL_QEXPR));
        LASSERT(val, val->cell[0]->count != 0, "Empty Expression: Function 'head' expects at least one value. Passed: {}");

        //Otherwise take first arg
        lval* newVal = lval_take(val, 0);

        //Delete all elements that are not the head and return
        while(newVal->count > 1) {
            lval_del(lval_pop(newVal, 1));
        }

        return newVal;
    }

    lval* builtin_tail(lenv* env, lval* val) {
        //Check error conditions
        LASSERT(val, val->count == 1, "Argument Error: Function 'tail' was passed too many arguments. Got: %i Expected: %i", val->count, 1);
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'tail' expects type Q-Expression. Got: %s Expected: %s", ltype_name(val->cell[0]->type), ltype_name(LVAL_QEXPR));
        LASSERT(val, val->cell[0]->count != 0, "Empty Expression: Function 'tail' expects at least one value. Passed: {}");

        //Otherwise take first arg
        lval* newVal = lval_take(val, 0);

        //Delete the first and return
        lval_del(lval_pop(newVal, 0));

        return newVal;
    }

    lval* builtin_list(lenv* env, lval* val) {
        val->type = LVAL_QEXPR;
        return val;
    }

    lval* builtin_eval(lenv* env, lval* val) {
        LASSERT(val, val->count == 1, "Argument Error: Function 'eval' passed too many arguments. Got: %i Expected: %i", val->count, 1);
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'eval' expects type Q-Expression. Got: %s Expected: %s", ltype_name(val->cell[0]->type), ltype_name(LVAL_QEXPR));

        lval* result = lval_take(val, 0);
        result->type = LVAL_SEXPR;

        return lval_eval(env, result);
    }

    lval* lval_join(lenv* env, lval* exp1, lval* exp2) {
        //For each cell in 'exp2' add it to 'exp1'
        while(exp2->count) {
            exp1 = lval_add(exp1, lval_pop(exp2, 0));
        }

        //Delete the empty 'exp2'
        lval_del(exp2);

        return exp1;
    }

    lval* lval_call(lenv* env, lval* func, lval* args) {
        //If builtin, then simply apply that
        if(func->builtin) {
            return func->builtin(env, args);
        }

        //Record arg counts
        int given = args->count;
        int total = func->formals->count;

        //While args still remain to be processed
        while(args->count) {
            //If we run out of formal args to bind
            if(func->formals->count == 0) {
                lval_del(args);
                return lval_err("Function passed too many arguments. Got %i, Expected %i", given, total);
            }

            //Pop the first symbol from the formals
            lval* sym = lval_pop(func->formals, 0);

            //Pop the next arg from the list
            lval* val = lval_pop(args, 0);

            //Bind a copy into the function's env
            lenv_set(func->env, sym, val);

            //Delete symbol and value
            lval_del(sym);
            lval_del(val);
        }

        //The arg list has been bound and can be cleaned up
        lval_del(args);

        //If all formals have been bound, evaluate
        if(func->formals->count == 0) {
            //Set env parent to evaluation env
            func->env->parent = env;

            //Evaluate and return
            return builtin_eval(func->env, lval_add(lval_sexpr(), lval_cpy(func->body)));
        } else {
            //Otherwise return partially evaluated function
            return lval_cpy(func);
        }
    }

    lval* builtin_join(lenv* env, lval* val) {
        for(int i = 0; i < val->count; i++) {
            LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function 'join' expects type Q-Expression. Got: %s", ltype_name(val->cell[0]->type));
        }

        lval* result = lval_pop(val, 0);

        while(val->count) {
            result = lval_join(env, result, lval_pop(val, 0));
        }

        lval_del(val);

        return result;
    }

    lval* builtin_var(lenv* env, lval* val, char* func) {
        LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Type Error: Function '%s' expects type Q-Expression. Got: %s", func, ltype_name(val->cell[0]->type));

        //First arg in symbol list
        lval* syms = val->cell[0];

        //Ensure all elements are symbols
        for(int i = 0; i < syms->count; i++) {
            LASSERT(val, syms->cell[i]->type == LVAL_SYM, "Define Error: Function '%s' cannot define non-symbol. Got: %s Expected: %s", func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
        }

        //Check correct number of symbols and vals
        LASSERT(val, syms->count == val->count - 1, "Define Error: Function '%s' expects equal number of values to symbols. Got: %i Expected: %i", func, syms->count, val->count-1);

        //Assign copies of vals to symbols
        for(int i = 0; i < syms->count; i++) {
            //If def define globally, if put define locally
            if(strcmp(func, "def") == 0) {
                lenv_def(env, syms->cell[i], val->cell[i+1]);
            } else if(strcmp(func, "=") == 0) {
                lenv_set(env, syms->cell[i], val->cell[i+1]);
            }
        }

        lval_del(val);
        return lval_sexpr();
    }

    lval* builtin_def(lenv* env, lval* args) {
        return builtin_var(env, args, "def");
    }

    lval* builtin_put(lenv* env, lval* args) {
        return builtin_var(env, args, "=");
    }

    lval* builtin_lambda(lenv* env, lval* args) {
        //Check two args each of which are Q-Exprs
        LASSERT_NUM("\\", args, 2);
        LASSERT_TYPE("\\", args, 0, LVAL_QEXPR);
        LASSERT_TYPE("\\", args, 1, LVAL_QEXPR);

        //Check first expression contains only symbols
        for(int i = 0; i < args->cell[0]->count; i++) {
            LASSERT(
                args,
                (args->cell[0]->cell[i]->type == LVAL_SYM),
                "Cannot define non-symbol. Got %s, Expected %s",
                ltype_name(args->cell[0]->cell[i]->type),
                ltype_name(LVAL_SYM)
            );
        }

        //Pop first 2 args and pass to lval_lambda
        lval* formals = lval_pop(args, 0);
        lval* body = lval_pop(args, 0);
        lval_del(args);

        return lval_lambda(formals, body);
    }

/* Arithemetic Builtins */
    lval* builtin_add(lenv* env, lval* args) {
        return builtin_op(env, args, "+");
    }

    lval* builtin_sub(lenv* env, lval* args) {
        return builtin_op(env, args, "-");
    }

    lval* builtin_mult(lenv* env, lval* args) {
        return builtin_op(env, args, "*");
    }

    lval* builtin_div(lenv* env, lval* args) {
        return builtin_op(env, args, "/");
    }

    lval* builtin_pow(lenv* env, lval* args) {
        return builtin_op(env, args, "^");
    }

    lval* builtin_mod(lenv* env, lval* args) {
        return builtin_op(env, args, "%");
    }

    lval* builtin(lenv* env, lval* val, char* func) {
        if (strcmp("list", func) == 0) { return builtin_list(env, val); }
        if (strcmp("head", func) == 0) { return builtin_head(env, val); }
        if (strcmp("tail", func) == 0) { return builtin_tail(env, val); }
        if (strcmp("join", func) == 0) { return builtin_join(env, val); }
        if (strcmp("eval", func) == 0) { return builtin_eval(env, val); }
        if (strstr("+-/*^", func)) { return builtin_op(env, val, func); }

        lval_del(val);

        return lval_err("Unknown Function!");
    }

/* Conditional Builtins */
    lval* builtin_ord(lenv* env, lval* args, char* op) {
        //Ensure all args are numbers
        LASSERT_NUM(op, args, 2);
        LASSERT_TYPE(op, args, 0, LVAL_NUM);
        LASSERT_TYPE(op, args, 1, LVAL_NUM);

        int cmpResult = 0;

        if(strcmp(op, ">") == 0) {
            cmpResult = (args->cell[0]->num > args->cell[1]->num);
        }

        if(strcmp(op, ">=") == 0) {
            cmpResult = (args->cell[0]->num >= args->cell[1]->num);
        }

        if(strcmp(op, "<") == 0) {
            cmpResult = (args->cell[0]->num < args->cell[1]->num);
        }

        if(strcmp(op, "<=") == 0) {
            cmpResult = (args->cell[0]->num <= args->cell[1]->num);
        }

        lval_del(args);

        return lval_num(cmpResult);
    }

    lval* builtin_gt(lenv* env, lval* args) {
        return builtin_ord(env, args, ">");
    }

    lval* builtin_gte(lenv* env, lval* args) {
        return builtin_ord(env, args, ">=");
    }

    lval* builtin_lt(lenv* env, lval* args) {
        return builtin_ord(env, args, "<");
    }

    lval* builtin_lte(lenv* env, lval* args) {
        return builtin_ord(env, args, "<=");
    }

    lval* lval_eq(lval* x, lval* y) {
        /* Different types are always unequal */
        if(x->type != y->type)
            return 0;

        //Compare base upon type
        switch(x->type) {
            //Compare num value
            case LVAL_NUM:
                return x->num == y->num;

            //Compare string vals
            case LVAL_ERR:
                return (strcmp(x->err, y->err) == 0);
            case LVAL_SYM:
                return (strcmp(x->symbol, y->symbol) == 0);

            //If builtin compare, otherwise compare formals and body
            case LVAL_FUN:
                if(x->builtin || y->builtin)
                    return x->builtin == y->builtin;
                else
                    return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);

            //If list compare each element
            case LVAL_QEXPR:
            case LVAL_SEXPR:
                //First check num of elements
                if(x->count != y->count)
                    return 0;

                for(int i = 0; i < x->count; i++) {
                    //If any element is not equal then bail
                    if(!lval_eq(x->cell[i], y->cell[i]))
                        return 0;
                }

                //Otherwise lists must be equal
                return 1;
                break;
        }

        //If we're here, bail since something went wrong
        return 0;
    }

    lval* builtin_cmp(lenv* env, lval* args, char* op) {
        LASSERT_NUM(op, args, 2);

        int cmpResult = 0;

        if(strcmp(op, "==") == 0) {
            cmpResult = lval_eq(args->cell[0], args->cell[1]);
        }

        if(strcmp(op, "!=") == 0) {
            cmpResult = !lval_eq(args->cell[0], args->cell[1]);
        }

        lval_del(args);

        return lval_num(cmpResult);
    }

    lval* builtin_eq(lenv* env, lval* args) {
        return builtin_cmp(env, args, "==");
    }

    lval* builtin_ne(lenv* env, lval* args) {
        return builtin_cmp(env, args, "!=");
    }

    lval* builtin_if(lenv* env, lval* args) {
        LASSERT_NUM("if", args, 3);
        LASSERT_TYPE("if", args, 0, LVAL_NUM);
        LASSERT_TYPE("if", args, 1, LVAL_QEXPR);
        LASSERT_TYPE("if", args, 2, LVAL_QEXPR);

        //Mark both expressions as evaluable
        lval* x;
        args->cell[1]->type = LVAL_SEXPR;
        args->cell[2]->type = LVAL_SEXPR;

        if(args->cell[0]->num) {
            //If condition is true evaluate first expression
            x = lval_eval(env, lval_pop(args, 1));
        } else {
            //Otherwise evaluate second expression
            x = lval_eval(env, lval_pop(args, 2));
        }

        //Delete argument list and return
        lval_del(args);

        return x;
    }

/* Add builtins to the environment */
    void lenv_add_builtin(lenv* env, char* name, lbuiltin func) {
        lval* k = lval_sym(name);
        lval* v = lval_fun(func);

        lenv_set(env, k, v);
        lval_del(k);
        lval_del(v);
    }

    void lenv_add_builtins(lenv* env) {
        //List functions
        lenv_add_builtin(env, "list", builtin_list);
        lenv_add_builtin(env, "head", builtin_head);
        lenv_add_builtin(env, "tail", builtin_tail);
        lenv_add_builtin(env, "eval", builtin_eval);
        lenv_add_builtin(env, "join", builtin_join);

        //Mathematical functions
        lenv_add_builtin(env, "+", builtin_add);
        lenv_add_builtin(env, "add", builtin_add);
        lenv_add_builtin(env, "-", builtin_sub);
        lenv_add_builtin(env, "sub", builtin_sub);
        lenv_add_builtin(env, "*", builtin_mult);
        lenv_add_builtin(env, "mult", builtin_mult);
        lenv_add_builtin(env, "/", builtin_div);
        lenv_add_builtin(env, "div", builtin_div);
        lenv_add_builtin(env, "^", builtin_pow);
        lenv_add_builtin(env, "pow", builtin_pow);
        lenv_add_builtin(env, "%", builtin_mod);
        lenv_add_builtin(env, "mod", builtin_mod);

        //Variable functions
        lenv_add_builtin(env, "\\",  builtin_lambda);
        lenv_add_builtin(env, "def", builtin_def);
        lenv_add_builtin(env, "=", builtin_put);

        //Comparison Functions
        lenv_add_builtin(env, "if", builtin_if);
        lenv_add_builtin(env, "==", builtin_eq);
        lenv_add_builtin(env, "!=", builtin_ne);
        lenv_add_builtin(env, ">",  builtin_gt);
        lenv_add_builtin(env, "<",  builtin_lt);
        lenv_add_builtin(env, ">=", builtin_gte);
        lenv_add_builtin(env, "<=", builtin_lte);
    }

    lval* lval_eval_sexpr(lenv* env, lval* val) {
        //Evaluate children
        for(int i = 0; i < val->count; i++) {
            val->cell[i] = lval_eval(env, val->cell[i]);
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
            return lval_eval(env, lval_take(val, 0));

        //Ensure first element is symbol
        lval* first = lval_pop(val, 0);

        if(first->type != LVAL_FUN) {
            lval* err = lval_err("S-Expression starts with incorrect type. Got %s, Expexted %s", ltype_name(first->type), ltype_name(LVAL_FUN));
            lval_del(first);
            lval_del(val);

            return err;
        }

        //Call builtin with operator
        lval* result = lval_call(env, first, val);
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

            case LVAL_FUN:
                if(val->builtin) {
                    printf("<function>");
                } else {
                    printf("(\\ ");
                    lval_print(val->formals);
                    putchar(' ');
                    lval_print(val->body);
                    putchar(')');
                }
                break;
        }
    }

    char* ltype_name(int type) {
        switch(type) {
            case LVAL_FUN: return "Function";
            case LVAL_NUM: return "Number";
            case LVAL_ERR: return "Error";
            case LVAL_SYM: return "Symbol";
            case LVAL_SEXPR: return "S-Expression";
            case LVAL_QEXPR: return "Q-Expression";
            default: return "Unknown";
        }
    }

    void lval_println(lval* val) {
        lval_print(val);
        putchar('\n');
    }

    lval* lval_cpy(lval* vals) {
        lval* result = malloc(sizeof(lval));
        result->type = vals->type;

        switch(vals->type) {
            //Copy functions and numbers directly
            case LVAL_FUN:
                if(vals->builtin) {
                    result->builtin = vals->builtin;
                } else {
                    result->builtin = NULL;
                    result->env = lenv_cpy(vals->env);
                    result->formals = lval_cpy(vals->formals);
                    result->body = lval_cpy(vals->body);
                }
                break;
            case LVAL_NUM:
                result->num = vals->num;
                break;

            //Copy strings with malloc and strcpy
            case LVAL_ERR:
                result->err = malloc(strlen(vals->err) + 1);
                strcpy(result->err, vals->err);
                break;

            case LVAL_SYM:
                result->symbol = malloc(strlen(vals->symbol) + 1);
                strcpy(result->symbol, vals->symbol);
                break;

            //Copy expressions by copying each sub-expression
            case LVAL_SEXPR:
            case LVAL_QEXPR:
                result->count = vals->count;
                result->cell = malloc(sizeof(lval*) * result->count);

                for(int i = 0; i < result->count; i++) {
                    result->cell[i] = lval_cpy(vals->cell[i]);
                }
            break;
        }

        return result;
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
        "                                                          \
            number   :  /-?[0-9]+/ ;                               \
            symbol   :  /[a-zA-Z0-9_+\\-*^\\/\\\\=<>!&]+/ ;         \
            sexpr    :  '(' <expr>* ')' ;                          \
            qexpr    :  '{' <expr>* '}' ;                          \
            expr     :  <number> | <symbol> | <sexpr> | <qexpr> ;  \
            lispy    :  /^/ <expr>* /$/ ;                          \
        ",
        Number, Symbol, Sexpr, Qexpr, Expr, Lispy
    );

    /* Print Version and Exit info */
    puts("Lispy Version 0.0.0.0.1");
    puts("Press Ctrl+C to Exit\n\n");

    lenv* env = lenv_new();
    lenv_add_builtins(env);

    /* Main program loop */
    while(1) {
        /* Output the prompt and get input - using editline for *nix */
        char* input = readline("danLISP>> ");

        /* Add input to history */
        add_history(input);

        /* Attempt to parse user input */
        mpc_result_t result;

        if(mpc_parse("<stdin>", input, Lispy, &result)) {
            /* Evaluate the Abstract Syntax Tree from output */
            lval* evalResult = lval_eval(env, lval_read(result.output));

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

    lenv_del(env);

    /* Clean up the parsers */
    mpc_cleanup(4, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

    return 0;
}
