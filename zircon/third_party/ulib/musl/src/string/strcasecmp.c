#include <ctype.h>
#include <strings.h>

#include "libc.h"

int strcasecmp(const char *_l, const char *_r) {
  const unsigned char *l = (void *)_l, *r = (void *)_r;
  for (; *l && *r && (*l == *r || tolower(*l) == tolower(*r)); l++, r++)
    ;
  return tolower(*l) - tolower(*r);
}
