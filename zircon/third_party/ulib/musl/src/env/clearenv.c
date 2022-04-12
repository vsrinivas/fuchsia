#define _GNU_SOURCE
#include <stdlib.h>

#include "libc.h"

int clearenv(void) {
  __environ[0] = 0;
  return 0;
}
