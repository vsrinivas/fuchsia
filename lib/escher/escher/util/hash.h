// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

// FNV-1a 32-bit Hash
//
// http://www.isthe.com/chongo/tech/comp/fnv/index.html
template <typename T>
struct Hash {
  std::size_t operator()(const T& hashee) const {
    constexpr uint32_t kPrime = 16777619;
    constexpr uint32_t kOffsetBasis = 2166136261;

    size_t len = sizeof(hashee);
    uint32_t n = kOffsetBasis;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&hashee);
    while (len-- > 0) {
      n = (n ^ *data) * kPrime;
      ++data;
    }
    return static_cast<std::size_t>(n);
  }
};

}  // namespace escher
