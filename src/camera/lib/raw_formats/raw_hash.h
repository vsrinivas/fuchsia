// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_HASH_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_HASH_H_

#include <type_traits>

namespace camera::raw::internal {

// Need to partially re-implement std::hash unfortunately since std::hash's opererator() doesn't
// have a constexpr version yet. This can go away in time.
template <typename T>
struct hash;

// Used to combine hashes produced by hash functions.
template <typename T>
constexpr inline void hash_combine(size_t& seed, const T& v) {
  hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Concept constrained partial specialization to hash any type trivially convertible to size_t.
template <typename T>
concept converts_to_size = std::is_convertible<T, size_t>::value;

template <converts_to_size T>
struct hash<T> {
  constexpr size_t operator()(T const& t) const noexcept { return t; }
};

// Concept constrained partial specialization for all enumeration types.
template <typename T>
concept enumeration = std::is_enum<T>::value;

template <enumeration T>
struct hash<T> {
  constexpr size_t operator()(T const& t) const noexcept { return static_cast<size_t>(t); }
};

}  // namespace camera::raw::internal

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_HASH_H_
