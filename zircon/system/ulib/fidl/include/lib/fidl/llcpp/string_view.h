// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_STRING_VIEW_H_
#define LIB_FIDL_LLCPP_STRING_VIEW_H_

#include <lib/fidl/walker.h>
#include <zircon/fidl.h>

#include <type_traits>

namespace fidl {

class StringView final : private fidl_string_t {
  // The maximum count to avoid colliding with the ownership bit.
  static constexpr uint64_t kMaxCount = internal::kVectorOwnershipMask - 1ULL;

 public:
  StringView() : fidl_string_t{} {}
  constexpr StringView(const char* data, uint64_t size)
      : fidl_string_t{size, const_cast<char*>(data)} {
    if (size > kMaxCount) {
      abort();
    }
  }

  // Constructs a fidl::StringView referencing a string literal. For example:
  //
  //     fidl::StringView view("hello");
  //     view.size() == 5;
  //
  template <size_t N>
  constexpr explicit StringView(const char (&literal)[N])
      : fidl_string_t{N - 1, const_cast<char*>(literal)} {
    static_assert(N > 0, "Empty string should be null-terminated");
  }

  // Creates a view over any container that implements |[const] char* data()| and |size()|.
  // E.g. an std::string.
  template <typename C, typename = decltype(std::declval<C&>().data()),
            typename = decltype(std::declval<C&>().size()),
            typename = std::enable_if_t<
                std::is_same<typename std::remove_const<typename std::remove_pointer<
                                 decltype(std::declval<C&>().data())>::type>::type,
                             char>::value>>
  explicit StringView(C& container)
      : fidl_string_t{container.size(), const_cast<char*>(container.data())} {}

  uint64_t size() const { return fidl_string_t::size; }
  void set_size(uint64_t size) {
    if (size > kMaxCount) {
      abort();
    }
    fidl_string_t::size = size;
  }

  const char* data() const { return fidl_string_t::data; }
  void set_data(const char* data) { fidl_string_t::data = const_cast<char*>(data); }

  bool is_null() const { return fidl_string_t::data == nullptr; }
  bool empty() const { return fidl_string_t::size == 0; }

  const char& at(size_t offset) const { return data()[offset]; }

  const char& operator[](size_t offset) const { return at(offset); }

  const char* begin() const { return data(); }
  const char* cbegin() const { return data(); }

  const char* end() const { return data() + size(); }
  const char* cend() const { return data() + size(); }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_STRING_VIEW_H_
