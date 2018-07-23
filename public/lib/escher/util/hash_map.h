// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_HASH_MAP_H_
#define LIB_ESCHER_UTIL_HASH_MAP_H_

#include <unordered_map>

#include "lib/escher/util/hash.h"
#include "lib/escher/util/hash_fnv_1a.h"

namespace escher {

// NOTE: if the hashed type is a struct, it must be tightly packed; if there are
// any padding bytes, their value will be undefined, and therefore the resulting
// hash value will also be undefined.  All types that are hashed by
// HashMapHasher should be added to hash_unittest.cc
//
// TODO(ES-107): Guarantee the padding assertion at compile time.
template <typename T, class Enable = void>
struct HashMapHasher {
  inline size_t operator()(const T& hashee) const {
    return hash_fnv_1a_64(reinterpret_cast<const uint8_t*>(&hashee),
                          sizeof(hashee));
  }
};

// Use SFINAE to provide a specialized implementation for any type that declares
// a HashMapHasher type.
template <typename T>
struct HashMapHasher<T, typename std::enable_if<std::is_class<
                            typename T::HashMapHasher>::value>::type> {
  inline size_t operator()(const T& hashee) const {
    typename T::HashMapHasher h;
    return h(hashee);
  }
};

// If the key is already a Hash, don't hash it again.
template <>
struct HashMapHasher<Hash, void> {
  inline size_t operator()(const Hash& hash) const { return hash.val; }
};

template <typename KeyT, typename ValueT>
using HashMap = std::unordered_map<KeyT, ValueT, HashMapHasher<KeyT>>;

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_HASH_MAP_H_
