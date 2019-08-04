#include <wchar.h>

#include "libc.h"

/* collate only by code points */
size_t wcsxfrm(wchar_t* restrict dest, const wchar_t* restrict src, size_t n) {
  size_t l = wcslen(src);
  if (l < n) {
    wmemcpy(dest, src, l + 1);
  } else if (n) {
    wmemcpy(dest, src, n - 1);
    dest[n - 1] = 0;
  }
  return l;
}
