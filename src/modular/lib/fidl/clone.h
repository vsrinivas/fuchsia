// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_FIDL_CLONE_H_
#define SRC_MODULAR_LIB_FIDL_CLONE_H_

#include <lib/fidl/cpp/vector.h>

#include <memory>

namespace modular {

template <typename T>
T CloneStruct(const T& value) {
  T new_value;
  value.Clone(&new_value);
  return new_value;
}

template <typename T>
std::unique_ptr<T> CloneOptional(const T& value) {
  T new_value;
  value.Clone(&new_value);
  return std::make_unique<T>(std::move(new_value));
}

template <typename T>
std::unique_ptr<T> CloneOptional(const std::unique_ptr<T>& value_ptr) {
  if (value_ptr) {
    T new_value;
    value_ptr->Clone(&new_value);
    return std::make_unique<T>(std::move(new_value));
  }
  return nullptr;
}

}  // namespace modular

#endif  // SRC_MODULAR_LIB_FIDL_CLONE_H_
