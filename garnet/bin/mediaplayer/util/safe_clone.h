// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_UTIL_SAFE_CLONE_H_
#define GARNET_BIN_MEDIAPLAYER_UTIL_SAFE_CLONE_H_

#include <memory>
#include <vector>

namespace media_player {

template <typename T>
std::unique_ptr<T> SafeClone(const std::unique_ptr<T>& t_ptr) {
  return t_ptr ? t_ptr.get()->Clone() : nullptr;
}

template <typename T>
std::unique_ptr<T> SafeClone(const T* t) {
  if (!t) {
    return nullptr;
  }

  auto result = T::New();
  t->Clone(result.get());
  return result;
}

template <typename T>
std::unique_ptr<T> SafeClone(const T& t) {
  auto result = T::New();
  t.Clone(result.get());
  return result;
}

template <typename T>
std::unique_ptr<std::vector<std::unique_ptr<T>>> SafeClone(
    const std::unique_ptr<std::vector<std::unique_ptr<T>>>& vec) {
  if (vec == nullptr) {
    return nullptr;
  }

  std::unique_ptr<std::vector<std::unique_ptr<T>>> result =
      std::unique_ptr<std::vector<std::unique_ptr<T>>>(
          new std::vector<std::unique_ptr<T>>(vec->size()));

  for (const std::unique_ptr<T>& t_ptr : *vec.get()) {
    result->push_back(SafeClone(t_ptr));
  }

  return result;
}

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_UTIL_SAFE_CLONE_H_
