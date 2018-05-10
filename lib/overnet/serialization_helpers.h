// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string.h>

namespace overnet {

inline uint8_t* WriteLE64(uint64_t x, uint8_t* bytes) {
  memcpy(bytes, &x, sizeof(x));
  return bytes + sizeof(x);
}

inline bool ParseLE64(const uint8_t** bytes, const uint8_t* end,
                      uint64_t* out) {
  if (end - *bytes < 8) return false;
  memcpy(out, *bytes, 8);
  *bytes += 8;
  return true;
}

}  // namespace overnet
