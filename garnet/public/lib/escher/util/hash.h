// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_HASH_H_
#define LIB_ESCHER_UTIL_HASH_H_

#include <cstdint>

namespace escher {

// Hash value is represented as a struct so that the type system can distinguish
// it from integer types.
struct Hash {
  uint64_t val;

  bool IsValid() const { return val != 0; }
  bool operator==(const Hash& other) const { return val == other.val; }
  bool operator!=(const Hash& other) const { return val != other.val; }
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_HASH_H_
