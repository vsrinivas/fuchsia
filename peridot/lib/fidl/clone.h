// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_CLONE_H_
#define PERIDOT_LIB_FIDL_CLONE_H_

#include <memory>

#include <lib/fidl/cpp/optional.h>
#include <lib/fidl/cpp/vector.h>

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

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_CLONE_H_
