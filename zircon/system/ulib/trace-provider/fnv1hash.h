// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/23079): De-dupe this.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FNV1HASH_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FNV1HASH_H_

#include <stdint.h>
#include <string.h>

// FNV-1a Hash
//
// http://www.isthe.com/chongo/tech/comp/fnv/index.html

#define FNV64_PRIME (1099511628211ULL)
#define FNV64_OFFSET_BASIS (14695981039346656037ULL)

static inline uint64_t fnv1a64(const void* ptr, size_t len) {
  uint64_t n = FNV64_OFFSET_BASIS;
  const uint8_t* data = (const uint8_t*)ptr;
  while (len-- > 0) {
    n = (n ^ (*data++)) * FNV64_PRIME;
  }
  return n;
}

#define fnv1a64str(str) fnv1a64(str, strlen(str))

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FNV1HASH_H_
