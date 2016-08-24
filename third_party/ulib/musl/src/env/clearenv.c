#define _GNU_SOURCE
#include <stdlib.h>

#include "libc.h"

int clearenv() {
    __environ[0] = 0;
    return 0;
}
