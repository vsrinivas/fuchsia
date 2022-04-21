// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>
#include <string.h>

#if !_ASM_MEMMOVE

typedef uintptr_t word;

#define lsize sizeof(word)
#define lmask (lsize - 1)

__attribute__((no_sanitize_address)) void *__unsanitized_memmove(void *dest, void const *src,
                                                                 size_t count) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  size_t len;

  if (count == 0 || dest == src)
    return dest;

  if ((uintptr_t)d < (uintptr_t)s) {
    if (((uintptr_t)d | (uintptr_t)s) & lmask) {
      // src and/or dest do not align on word boundary
      if ((((uintptr_t)d ^ (uintptr_t)s) & lmask) || (count < lsize))
        len = count;  // copy the rest of the buffer with the byte mover
      else
        len = lsize - ((uintptr_t)d & lmask);  // move the ptrs up to a word boundary

      count -= len;
      for (; len > 0; len--)
        *d++ = *s++;
    }
    for (len = count / lsize; len > 0; len--) {
      *(word *)d = *(word *)s;
      d += lsize;
      s += lsize;
    }
    for (len = count & lmask; len > 0; len--)
      *d++ = *s++;
  } else {
    d += count;
    s += count;
    if (((uintptr_t)d | (uintptr_t)s) & lmask) {
      // src and/or dest do not align on word boundary
      if ((((uintptr_t)d ^ (uintptr_t)s) & lmask) || (count <= lsize))
        len = count;
      else
        len = ((uintptr_t)d & lmask);

      count -= len;
      for (; len > 0; len--)
        *--d = *--s;
    }
    for (len = count / lsize; len > 0; len--) {
      d -= lsize;
      s -= lsize;
      *(word *)d = *(word *)s;
    }
    for (len = count & lmask; len > 0; len--)
      *--d = *--s;
  }

  return dest;
}

// Make the function a weak symbol so asan can override it.
__typeof(__unsanitized_memmove) memmove __attribute__((weak, alias("__unsanitized_memmove")));

#endif
