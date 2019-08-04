#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char digits[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

long a64l(const char* s) {
  int e;
  uint32_t x = 0;
  for (e = 0; e < 36 && *s; e += 6, s++)
    x |= (strchr(digits, *s) - digits) << e;
  return x;
}
