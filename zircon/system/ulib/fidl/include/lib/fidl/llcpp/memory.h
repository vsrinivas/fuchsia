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

// TODO(fxbug.dev/42059): Create additional helpers for copying LLCPP objects.

namespace {

template <typename ElemType>
void op_copy(std::remove_const_t<ElemType>* out, const ElemType& in) {
  *out = in;
}

template <typename ElemType, typename ContainerType, typename Op>
void iterate_do(ElemType* out, ContainerType& in, const Op op) {
  for (auto& in_elem : in) {
    op(out, in_elem);
    ++out;
  }
}

// Implementation for heap_move and heap_copy.
template <typename ElemType, typename ContainerType, typename Op>
fidl::VectorView<ElemType> heap_op(ContainerType& container, Op op) {
  size_t size = std::size(container);
  // Don't use std::make_unique to avoid the cost of constructing the array elements.
  auto array = new std::remove_const_t<ElemType>[size];
  std::unique_ptr<std::remove_const_t<ElemType>[]> uptr(array);
  iterate_do(uptr.get(), container, op);
  return fidl::VectorView(std::move(uptr), size);
}

// Implementation for heap_move and heap_copy.
template <typename ElemType, typename ContainerType, typename Op>
fidl::VectorView<ElemType> allocator_op(fidl::Allocator& allocator, ContainerType& container,
                                        Op op) {
  size_t size = std::size(container);
  // TODO(fxbug.dev/42059): Support the equivalent of std::make_unique_for_overwrite.
  auto ptr = allocator.make<std::remove_const_t<ElemType>[]>(size);
  iterate_do(ptr.get(), container, op);
  return fidl::VectorView(std::move(ptr), size);
}

}  // namespace

namespace fidl {

// Construct a vector_view from a container supporting std::size and std:data using a heap
// allocated array. The internal array elements will be copied to the new array.
//
// Example:
// std::vector<uint32_t> vec;
// VectorView<uint32_t> vv = fidl::heap_copy_vec(vec);
template <
    typename T, typename = decltype(std::data(std::declval<T&>())),
    typename = decltype(std::size(std::declval<T&>())),
    typename = decltype(std::cbegin(std::declval<T&>())),
    typename = decltype(std::cend(std::declval<T&>())),
    typename ElemType = typename std::remove_pointer<decltype(std::data(std::declval<T&>()))>::type>
VectorView<ElemType> heap_copy_vec(const T& container) {
  return heap_op<ElemType>(container, op_copy<ElemType>);
}

// Construct a vector_view from a container supporting std::size and std::data using an array
// allocated with a fidl::Allocator. The internal array elements will be copied to the new array.
//
// Example:
// std::vector<uint32_t> vec;
// VectorView<uint32_t> vv = fidl::copy_vec(vec);
template <
    typename T, typename = decltype(std::data(std::declval<T&>())),
    typename = decltype(std::size(std::declval<T&>())),
    typename = decltype(std::cbegin(std::declval<T&>())),
    typename = decltype(std::cend(std::declval<T&>())),
    typename ElemType = typename std::remove_pointer<decltype(std::data(std::declval<T&>()))>::type>
VectorView<ElemType> copy_vec(fidl::Allocator& allocator, const T& container) {
  return allocator_op<ElemType>(allocator, container, op_copy<ElemType>);
}

// TODO(fxbug.dev/42059): Add support for heap_move and move.

// Construct a StringView from a container using a heap allocated array.
//
// Example:
// std::string str;
// StringView sv = fidl::heap_copy_str(str);
template <typename T, typename = decltype(std::data(std::declval<T&>())),
          typename = decltype(std::size(std::declval<T&>())),
          typename = std::enable_if_t<!std::is_array<T>::value>>
StringView heap_copy_str(const T& container) {
  return StringView(std::move(heap_copy_vec(container)));
}

// Construct a StringView from a c-string using a heap allocated array.
//
// Example:
// char * str = "hello world";
// StringView sv = fidl::heap_copy_str(str, 2);
template <typename = std::enable_if_t<true>>  // avoid symbol-redefinition
StringView heap_copy_str(const char* str, size_t len) {
  std::unique_ptr<const char[]> buf = std::make_unique<const char[]>(len);
  memcpy(const_cast<char*>(buf.get()), str, len);
  return StringView(std::move(buf), len);
}

// Construct a StringView from a container supporting std::size and std::data using a
// fidl::Allocator-allocated array.
//
// Example:
// fidl::BufferThenHeapAllocator<2048> allocator;
// std::string str;
// StringView sv = fidl::copy_str(allocator, str);
template <typename T, typename = decltype(std::data(std::declval<T&>())),
          typename = decltype(std::size(std::declval<T&>())),
          typename = std::enable_if_t<!std::is_array<T>::value>>
StringView copy_str(fidl::Allocator& allocator, T& container) {
  return StringView(std::move(copy_vec(allocator, container)));
}

// Construct a StringView from a c-string using a fidl::Allocator-allocated array.
//
// Example:
// fidl::BufferThenHeapAllocator<2048> allocator;
// char * str = "hello world";
// StringView sv = fidl::heap_copy_str(allocator, str, 2);
template <typename = std::enable_if_t<true>>  // avoid symbol-redefinition
StringView copy_str(fidl::Allocator& allocator, const char* str, size_t len) {
  tracking_ptr<const char[]> buf = allocator.make<const char[]>(len);
  memcpy(const_cast<char*>(buf.get()), str, len);
  return StringView(std::move(buf), len);
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MEMORY_H_
