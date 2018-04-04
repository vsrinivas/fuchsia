// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "lib/escher/util/hash_fnv_1a.h"

namespace escher {

// NOTE: if the hashed type is a struct, it must be tightly packed; if there are
// any padding bytes, their value will be undefined, and therefore the resulting
// hash value will also be undefined.  All types that are hashed by
// HashMapHasher should be added to hash_unittest.cc
template <typename T, class Enable = void>
struct HashMapHasher {
  inline size_t operator()(const T& hashee) const {
    return hash_fnv_1a_64(reinterpret_cast<const uint8_t*>(&hashee),
                          sizeof(hashee));
  }
};

// Use SFINAE to provide a specialized implementation for any type that declares
// a Hash type.
template <typename T>
struct HashMapHasher<
    T,
    typename std::enable_if<std::is_class<typename T::Hash>::value>::type> {
  inline size_t operator()(const T& hashee) const {
    typename T::Hash h;
    return h(hashee);
  }
};

template <typename KeyT, typename ValueT>
using HashMap = std::unordered_map<KeyT, ValueT, HashMapHasher<KeyT>>;

}  // namespace escher
