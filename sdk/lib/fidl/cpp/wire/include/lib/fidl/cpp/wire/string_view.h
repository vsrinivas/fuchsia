// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_STRING_VIEW_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_STRING_VIEW_H_

#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fidl/walker.h>
#include <zircon/fidl.h>

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace fidl {

// A FIDL string that borrows its contents.
class StringView final : private VectorView<const char> {
 public:
  StringView() : VectorView() {}
  explicit StringView(VectorView<char>&& vv) : VectorView(std::move(vv)) {}
  explicit StringView(VectorView<const char>&& vv) : VectorView(std::move(vv)) {}

  // Allocates a string using an arena.
  StringView(AnyArena& allocator, std::string_view from) : VectorView(allocator, from.size()) {
    memcpy(const_cast<char*>(VectorView::data()), from.data(), from.size());
  }

  // Constructs a fidl::StringView referencing a string literal. For example:
  //
  //     fidl::StringView view("hello");
  //     view.size() == 5;
  //
  template <size_t N>
  constexpr StringView(const char (&literal)[N], uint64_t size = N - 1)
      : VectorView(static_cast<const char*>(literal), size) {
    static_assert(N > 0, "String should not be empty");
  }

  // These methods are the only way to reference data which is not managed by a Arena.
  // Their usage is discouraged. The lifetime of the referenced string must be longer than the
  // lifetime of the created StringView.
  //
  // For example:
  // std::string foo = path + "/foo";
  // fidl::StringView foo_view(foo);
  static StringView FromExternal(std::string_view from) { return StringView(from); }
  static StringView FromExternal(const char* data, size_t size) { return StringView(data, size); }

  void Set(AnyArena& allocator, std::string_view from) {
    Allocate(allocator, from.size());
    memcpy(const_cast<char*>(VectorView::data()), from.data(), from.size());
  }

  std::string_view get() const { return {data(), size()}; }

  uint64_t size() const { return count(); }
  void set_size(uint64_t size) { set_count(size); }

  const char* data() const { return VectorView::data(); }

  // Returns if the string view is empty.
  bool empty() const { return size() == 0; }

  // TODO(fxbug.dev/109737): |is_null| is used to check if an optional view type
  // is absent. This can be removed if optional view types switch to
  // |fidl::WireOptional|.
  bool is_null() const { return data() == nullptr; }

  const char& at(size_t offset) const { return data()[offset]; }
  const char& operator[](size_t offset) const { return at(offset); }

  const char* begin() const { return data(); }
  const char* cbegin() const { return data(); }

  const char* end() const { return data() + size(); }
  const char* cend() const { return data() + size(); }

 private:
  explicit StringView(std::string_view from) : VectorView(from.data(), from.size()) {}
  StringView(const char* data, uint64_t size) : VectorView(data, size) {}
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_STRING_VIEW_H_
