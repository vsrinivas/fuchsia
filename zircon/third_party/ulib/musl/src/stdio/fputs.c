#include <string.h>

#include "stdio_impl.h"

int fputs(const char* restrict s, FILE* restrict f) {
  size_t l = strlen(s);
  return (fwrite(s, 1, l, f) == l) - 1;
}

weak_alias(fputs, fputs_unlocked);
