// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The fidl::unowned family of helpers facillitate creating LLCPP objects pointing to memory that
// is not owned by the LLCPP objects.
// By design, unowned references to memory must be explicitly specified so that the user is aware
// of the fact that this can be unsafe. Alternative approaches that can be used are
// std::make_unique, allocators and fidl::heap_copy, which is analogous to fidl::unowned but copies
// allocations to LLCPP owned areas.

#ifndef LIB_FIDL_LLCPP_MEMORY_H_
#define LIB_FIDL_LLCPP_MEMORY_H_

#include <iterator>

#include "allocator.h"
#include "string_view.h"
#include "vector_view.h"

namespace fidl {

// Create a unowned_ptr_t from a raw pointer, which can be used to construct a tracking_ptr.
//
// This makes code less verbose by inferring the unowned_ptr_t type. Better type inference directly
// on unique_ptr depends on C++17 features like class type deduction and deduction rules.
//
// Example:
// uint32_t x;
// tracking_ptr<uint32_t> ptr = fidl::unowned_ptr(x);
template <typename T, typename ElemType = std::remove_extent_t<T>>
unowned_ptr_t<ElemType> unowned_ptr(T* ptr) {
  return unowned_ptr_t<T>(ptr);
}

// Construct a VectorView from a container supporting std::size and std::data using an unowned_ptr_t
// to the internal container data.
//
// Example:
// std::vector<uint32_t> vec;
// VectorView<uint32_t> vv = fidl::unowned_vec(vec);
template <
    typename T, typename = decltype(std::data(std::declval<T&>())),
    typename = decltype(std::size(std::declval<T&>())),
    typename ElemType = typename std::remove_pointer<decltype(std::data(std::declval<T&>()))>::type>
VectorView<ElemType> unowned_vec(T& container) {
  return VectorView(fidl::unowned_ptr(std::data(container)), std::size(container));
}

// Construct a StringView from a container supporting std::size and std::data using an unowned_ptr_t
// to the internal container data.
//
// Example:
// std::string str;
// StringView sv = fidl::unowned_str(str);
template <typename T, typename = decltype(std::data(std::declval<T&>())),
          typename = decltype(std::size(std::declval<T&>())),
          typename = std::enable_if_t<!std::is_array<T>::value>>
StringView unowned_str(const T& container) {
  return StringView(fidl::unowned_ptr(std::data(container)), std::size(container));
}

// Construct a StringView from a c-string using an unowned_ptr_t to the data.
//
// Example:
// char * str = "hello world";
// StringView sv = fidl::unowned_str(str, 2);
template <typename = std::enable_if_t<true>>  // avoid symbol-redefinition
StringView unowned_str(const char* str, size_t len) {
  return StringView(fidl::unowned_ptr(str), len);
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MEMORY_H_
