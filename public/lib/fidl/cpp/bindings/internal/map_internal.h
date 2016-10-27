// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_INTERNAL_H_

#include <map>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/internal/template_util.h"

namespace fidl {
namespace internal {

template <typename Key, typename Value, bool kValueIsMoveOnlyType>
struct MapTraits {};

// Defines traits of a map for which Value is not a move-only type.
template <typename Key, typename Value>
struct MapTraits<Key, Value, false> {
  typedef const Value& ValueForwardType;

  static inline void Insert(std::map<Key, Value>* m,
                            const Key& key,
                            ValueForwardType value) {
    m->insert(std::make_pair(key, value));
  }
  static inline void Clone(const std::map<Key, Value>& src,
                           std::map<Key, Value>* dst) {
    dst->clear();
    for (auto it = src.begin(); it != src.end(); ++it)
      dst->insert(*it);
  }
};

// Defines traits of a map for which Value is a move-only type.
template <typename Key, typename Value>
struct MapTraits<Key, Value, true> {
  typedef Value ValueForwardType;

  static inline void Insert(std::map<Key, Value>* m,
                            const Key& key,
                            Value& value) {
    m->insert(std::make_pair(key, std::move(value)));
  }
  static inline void Clone(const std::map<Key, Value>& src,
                           std::map<Key, Value>* dst) {
    dst->clear();
    for (auto it = src.begin(); it != src.end(); ++it)
      dst->insert(std::make_pair(it->first, it->second.Clone()));
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_INTERNAL_H_
