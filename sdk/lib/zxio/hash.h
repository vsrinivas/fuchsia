// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_HASH_H_
#define LIB_ZXIO_HASH_H_

#include <unistd.h>

#include <functional>

// Adapted from: https://www.boost.org/doc/libs/1_64_0/boost/functional/hash/hash.hpp.
template <class T>
void hash_combine(size_t& seed, const T& v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

#endif  // LIB_ZXIO_HASH_H_
