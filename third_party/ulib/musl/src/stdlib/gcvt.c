#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

char* gcvt(double x, int n, char* b) {
    sprintf(b, "%.*g", n, x);
    return b;
}
