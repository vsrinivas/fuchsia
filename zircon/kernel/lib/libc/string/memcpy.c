// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

typedef long word;

#define lsize sizeof(word)
#define lmask (lsize - 1)

__attribute__((no_sanitize_address)) void *__unsanitized_memcpy(void *dest, const void *src,
                                                                size_t count) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  int len;

  if (count == 0 || dest == src)
    return dest;

  if (((long)d | (long)s) & lmask) {
    // src and/or dest do not align on word boundary
    if ((((long)d ^ (long)s) & lmask) || (count < lsize))
      len = count;  // copy the rest of the buffer with the byte mover
    else
      len = lsize - ((long)d & lmask);  // move the ptrs up to a word boundary

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

  return dest;
}

// Make the function a weak symbol so asan can override it.
__typeof(__unsanitized_memcpy) memcpy __attribute__((weak, alias("__unsanitized_memcpy")));
