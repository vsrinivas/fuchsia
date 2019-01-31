#include <signal.h>
#include <stdio.h>
#include <string.h>

void psignal(int sig, const char* msg) {
    char* s = strsignal(sig);
    if (msg)
        fprintf(stderr, "%s: %s\n", msg, s);
    else
        fprintf(stderr, "%s\n", s);
}
