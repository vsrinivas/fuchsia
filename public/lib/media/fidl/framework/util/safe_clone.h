// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_SAFE_CLONE_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_SAFE_CLONE_H_

#include <memory>
#include <vector>

namespace mojo {
namespace media {

template <typename T>
std::unique_ptr<T> SafeClone(const std::unique_ptr<T>& t_ptr) {
  return t_ptr ? t_ptr.get()->Clone() : nullptr;
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

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_SAFE_CLONE_H_
