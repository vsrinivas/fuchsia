// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_UTIL_SAFE_CLONE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_UTIL_SAFE_CLONE_H_

#include <lib/fidl/cpp/optional.h>
#include <lib/fidl/cpp/vector.h>

#include <memory>

namespace media_player {

template <typename T>
std::unique_ptr<T> SafeClone(const std::unique_ptr<T>& t_ptr) {
  return t_ptr ? t_ptr.get()->Clone() : nullptr;
}

template <typename T>
std::unique_ptr<T> SafeClone(const T* t) {
  return t ? t->Clone() : nullptr;
}

template <typename T>
std::unique_ptr<T> CloneOptional(const T& value) {
  T new_value;
  value.Clone(&new_value);
  return fidl::MakeOptional<T>(std::move(new_value));
}

template <typename T>
std::unique_ptr<T> CloneOptional(const std::unique_ptr<T>& value_ptr) {
  if (value_ptr) {
    T new_value;
    value_ptr->Clone(&new_value);
    return fidl::MakeOptional<T>(std::move(new_value));
  }
  return nullptr;
}

template <typename T>
std::unique_ptr<T> CloneOptional(const T* value_ptr) {
  if (value_ptr) {
    T new_value;
    value_ptr->Clone(&new_value);
    return fidl::MakeOptional<T>(std::move(new_value));
  }
  return nullptr;
}

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_UTIL_SAFE_CLONE_H_
