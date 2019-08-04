#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

#include "libc.h"

int euidaccess(const char* filename, int amode) {
  return faccessat(AT_FDCWD, filename, amode, AT_EACCESS);
}

weak_alias(euidaccess, eaccess);
