// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_HASH_FNV_1A_H_
#define LIB_ESCHER_UTIL_HASH_FNV_1A_H_

#include <cstdint>

namespace escher {

// Constants for 64-bit FNV-1a hash function.  See below.
constexpr uint64_t kHashFnv1Prime64 = 1099511628211ull;
constexpr uint64_t kHashFnv1OffsetBasis64 = 14695981039346656037ull;

// FNV-1a 64-bit Hash (http://www.isthe.com/chongo/tech/comp/fnv/index.html)
inline uint64_t hash_fnv_1a_64(const uint8_t* data, size_t len,
                               uint64_t previous = kHashFnv1OffsetBasis64) {
  uint64_t n = previous;
  while (len-- > 0) {
    n = (n ^ *data) * kHashFnv1Prime64;
    ++data;
  }
  return n;
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_HASH_FNV_1A_H_
