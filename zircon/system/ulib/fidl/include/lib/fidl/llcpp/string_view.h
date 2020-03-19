// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_STRING_VIEW_H_
#define LIB_FIDL_LLCPP_STRING_VIEW_H_

#include <lib/fidl/walker.h>
#include <zircon/fidl.h>

#include <type_traits>

#include "vector_view.h"

namespace fidl {

class StringView final : private VectorView<const char> {
 public:
  StringView() : VectorView() {}
  StringView(tracking_ptr<const char[]>&& data, uint64_t size)
      : VectorView(std::move(data), size) {}
  explicit StringView(VectorView<char>&& vv) : VectorView(std::move(vv)) {}
  explicit StringView(VectorView<const char>&& vv) : VectorView(std::move(vv)) {}

  // Constructs a fidl::StringView referencing a string literal. For example:
  //
  //     fidl::StringView view("hello");
  //     view.size() == 5;
  //
  template <size_t N>
  constexpr StringView(const char (&literal)[N], uint64_t size = N - 1)
      : VectorView(fidl::unowned_ptr_t<const char>(literal), size) {
    static_assert(N > 0, "String should not be empty");
  }

  uint64_t size() const { return count(); }
  void set_size(uint64_t size) { set_count(size); }

  const char* data() const { return VectorView::data(); }
  void set_data(tracking_ptr<const char[]> data) { VectorView::set_data(std::move(data)); }

  bool is_null() const { return data() == nullptr; }
  bool empty() const { return size() == 0; }

  const char& at(size_t offset) const { return data()[offset]; }

  const char& operator[](size_t offset) const { return at(offset); }

  const char* begin() const { return data(); }
  const char* cbegin() const { return data(); }

  const char* end() const { return data() + size(); }
  const char* cend() const { return data() + size(); }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_STRING_VIEW_H_
