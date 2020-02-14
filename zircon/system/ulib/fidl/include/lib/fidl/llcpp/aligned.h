// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ALIGNED_H_
#define LIB_FIDL_LLCPP_ALIGNED_H_

#include <zircon/fidl.h>

#include <type_traits>
#include <utility>

namespace fidl {

// fidl::aligned wraps and aligns values to FIDL_ALIGNMENT.
//
// This enables 1-byte values like uint8_t, int8_t and bool to be pointed
// to by tracking_ptr as unowned memory. Heap allocated values do not need
// fidl::aligned because they are already aligned to std::max_align_t.
//
// Usage:
// fidl::aligned<uint8_t> x = 5;
template <typename T>
struct aligned final {
 public:
  template <typename... Args>
  aligned(Args&&... args) : value(std::forward<Args>(args)...) {}

  aligned& operator=(T&& v) {
    value = std::forward<T>(v);
    return *this;
  }

  operator typename std::decay_t<T>() const { return value; }

  alignas(FIDL_ALIGNMENT) T value;
};

static_assert(std::alignment_of<aligned<uint8_t>>::value == FIDL_ALIGNMENT,
              "alignment of aligned objects should be FIDL_ALIGNMENT");

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ALIGNED_H_
