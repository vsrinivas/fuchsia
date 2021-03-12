// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ctype.h>
#include <printf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>

#define ATOx(T, fn)                   \
  T fn(const char* nptr) {            \
    while (nptr && isspace(*nptr)) {  \
      nptr++;                         \
    }                                 \
                                      \
    bool neg = false;                 \
    if (*nptr == '-') {               \
      neg = true;                     \
      nptr++;                         \
    }                                 \
                                      \
    T ret = 0;                        \
    for (; nptr; nptr++) {            \
      if (!isdigit(*nptr))            \
        break;                        \
      ret = 10 * ret + (*nptr - '0'); \
    }                                 \
                                      \
    if (neg)                          \
      ret = -ret;                     \
    return ret;                       \
  }

ATOx(int, atoi) ATOx(long, atol) ATOx(long long, atoll)

    void* malloc(size_t size) {
  void* addr = NULL;
  if (gBS->AllocatePool(EfiLoaderData, size, &addr) != EFI_SUCCESS) {
    printf("%s: failed to allocate %zu bytes\n", __func__, size);
    return NULL;
  }
  return addr;
}

void* calloc(size_t num, size_t size) {
  const size_t total_size = num * size;
  void* addr = malloc(total_size);
  if (addr) {
    memset(addr, 0, total_size);
  }
  return addr;
}

void free(void* addr) {
  if (addr && gBS->FreePool(addr) != EFI_SUCCESS) {
    printf("%s: failed to free memory at %p\n", __func__, addr);
  }
}

void* memmove(void* dest, const void* src, size_t count) {
  // EFI CopyMem() function can handle overlapping buffers.
  gBS->CopyMem(dest, src, count);
  return dest;
}

void __chkstk(void) {}

void abort(void) {
  printf("Fatal: abort() called\n");
  assert(false);
}
