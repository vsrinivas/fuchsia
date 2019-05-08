// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_PAIR_HASHER_H_
#define SRC_UI_LIB_ESCHER_UTIL_PAIR_HASHER_H_

#include <utility>

#include "src/ui/lib/escher/util/bit_ops.h"

namespace escher {

// Provides a hash operator that allows std::pair to be used e.g. as a key in a
// std::unordered_map.  NOTE: this can be deleted when a future C++ standard
// library provides a default hash operator for pairs.
struct PairHasher {
  template <typename T1, typename T2>
  size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1>()(p.first);
    auto h2 = std::hash<T2>()(p.second);
    // Cannot simply XOR the hashes together because symmetric values (e.g.
    // std::make_pair(25, 25)) would always hash to zero, and there would be
    // hash collisions between std::make_pair(25, 26) and std::make_pair(26,25).
    return RotateLeft(h1, 1) ^ h2;
  }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_PAIR_HASHER_H_
