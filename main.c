#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char** argv) {

    /* Print Version and Exit info */
    puts("Lispy Version 0.0.0.0.1");
    puts("Press Ctrl+C to Exit\n\n");

    /* Main program loop */
    while(1) {
        /* Output the prompt and get input - using editline for *nix */
        char* input = readline("lispy> ");

        /* Add input to history */
        add_history(input);

        /* Echo input back to user */
        printf("No, you're a %s\n", input);

        /* Free retrieved input */
        free(input);
    }

    return 0;
}
