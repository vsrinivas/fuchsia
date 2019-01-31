#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>

void herror(const char* msg) {
    fprintf(stderr, "%s%s%s", msg ? msg : "", msg ? ": " : "", hstrerror(h_errno));
}
