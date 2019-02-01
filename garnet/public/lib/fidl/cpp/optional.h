// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_OPTIONAL_H_
#define LIB_FIDL_CPP_OPTIONAL_H_

#include <memory>
#include <utility>

namespace fidl {

template <typename T>
std::unique_ptr<T> MakeOptional(T value) {
  return std::make_unique<T>(std::move(value));
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_OPTIONAL_H_
