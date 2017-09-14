// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_INTERNAL_H_

#include <type_traits>

#include <zx/channel.h>
#include <zx/object.h>

#include "lib/fidl/cpp/bindings/internal/template_util.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"

namespace fidl {
class String;

template <typename T>
class Array;

template <typename Interface>
class InterfaceHandle;

template <typename Interface>
class InterfaceRequest;

template <typename K, typename V>
class Map;

namespace internal {
template <typename T>
class Array_Data;

#pragma pack(push, 1)

struct StructHeader {
  uint32_t num_bytes;
  uint32_t version;
};
static_assert(sizeof(StructHeader) == 8, "Bad sizeof(StructHeader)");

struct ArrayHeader {
  uint32_t num_bytes;
  uint32_t num_elements;
};
static_assert(sizeof(ArrayHeader) == 8, "Bad_sizeof(ArrayHeader)");

template <typename T>
union StructPointer {
  uint64_t offset;
  T* ptr;
};
static_assert(sizeof(StructPointer<char>) == 8, "Bad_sizeof(StructPointer)");

template <typename T>
union ArrayPointer {
  uint64_t offset;
  Array_Data<T>* ptr;
};
static_assert(sizeof(ArrayPointer<char>) == 8, "Bad_sizeof(ArrayPointer)");

union StringPointer {
  uint64_t offset;
  Array_Data<char>* ptr;
};
static_assert(sizeof(StringPointer) == 8, "Bad_sizeof(StringPointer)");

struct WrappedHandle {
  zx_handle_t value;
};
static_assert(sizeof(WrappedHandle) == 4, "Bad_sizeof(WrappedHandle)");

struct Interface_Data {
  WrappedHandle handle;
  uint32_t version;
};
static_assert(sizeof(Interface_Data) == 8, "Bad_sizeof(Interface_Data)");

template <typename T>
union UnionPointer {
  uint64_t offset;
  T* ptr;
};
static_assert(sizeof(UnionPointer<char>) == 8, "Bad_sizeof(UnionPointer)");

#pragma pack(pop)

template <typename T>
void ResetIfNonNull(T* ptr) {
  if (ptr)
    *ptr = T();
}

template <typename T>
T FetchAndReset(T* ptr) {
  T temp = *ptr;
  *ptr = T();
  return temp;
}

template <typename T>
T UnwrapHandle(const WrappedHandle& handle) {
  return T(zx::handle(handle.value));
}

template <typename T>
struct IsWrappedHandle {
  static const bool value = false;
};

template <>
struct IsWrappedHandle<WrappedHandle> {
  static const bool value = true;
};

static_assert(internal::IsWrappedHandle<WrappedHandle>::value,
    "|struct WrappedHandle| must be a valid handle encoding.");
static_assert(!internal::IsWrappedHandle<int>::value,
    "|int| must be a valid handle encoding.");
static_assert(!internal::IsWrappedHandle<zx_handle_t>::value,
    "|zx_handle_t| must be a valid handle encoding.");
static_assert(!internal::IsWrappedHandle<zx::channel>::value,
    "|zx::channel| must be a valid handle encoding.");

// TODO(vardhan): Replace RemoveStructPtr<> and UnwrapStructPtr<> with
// specializations of std::pointer_traits<> on [Inlined]StructPtr<>.
template <typename T>
struct RemoveStructPtr {
  typedef T type;
};

template <typename T>
struct RemoveStructPtr<StructPtr<T>> {
  typedef T type;
};

template <typename T>
struct RemoveStructPtr<InlinedStructPtr<T>> {
  typedef T type;
};

template <typename T>
struct UnwrapStructPtr {
  static T* value(T& x) { return &x; }
};

template <typename T>
struct UnwrapStructPtr<StructPtr<T>> {
  static T* value(StructPtr<T>& x) { return x.get(); }
};

template <typename T>
struct UnwrapStructPtr<InlinedStructPtr<T>> {
  static T* value(InlinedStructPtr<T>& x) { return x.get(); }
};

template <typename T>
struct UnwrapConstStructPtr {
  static const T* value(const T& x) { return &x; }
};

template <typename T>
struct UnwrapConstStructPtr<StructPtr<T>> {
  static const T* value(const StructPtr<T>& x) { return x.get(); }
};

template <typename T>
struct UnwrapConstStructPtr<InlinedStructPtr<T>> {
  static const T* value(const InlinedStructPtr<T>& x) { return x.get(); }
};

template <typename T>
struct IsStructPtr {
  static bool const value = IsSpecializationOf<StructPtr, T>::value ||
                            IsSpecializationOf<InlinedStructPtr, T>::value;
};

template <typename T>
struct IsUnionWrapperType {
  template <typename U>
  static YesType Test(const typename U::Data_::MojomUnionDataType*);

  template <typename U>
  static NoType Test(...);

  static const bool value =
      sizeof(Test<T>(0)) == sizeof(YesType) && !std::is_const<T>::value;
};

template <typename T>
struct IsUnionDataType {
  template <typename U>
  static YesType Test(const typename U::MojomUnionDataType*);

  template <typename U>
  static NoType Test(...);

  static const bool value =
      sizeof(Test<T>(0)) == sizeof(YesType) && !std::is_const<T>::value;
};

// To introduce a new mojom type, you must define (partial or full) template
// specializations for the following traits templates, which operate on the C++
// wrapper types representing a mojom type:
//   - WrapperTraits: provides a |DataType| typedef which points to the
//                    serialized data type.
//   - ValueTraits: provides an |Equals()| method for comparing the two wrapper
//                  values of the same type.
//
// Currently, full specializations of WrapperTraits are generated for mojo
// structs in their generated `.mojom.h`. In contrast, WrapperTraits for Unions
// don't need to be auto-generated because their underlying DataType can be
// accessed without dependency issues -- a catch-all WrapperTraits for unions is
// instead defined here.
// TODO(vardhan): Merge ValueTraits and WrapperTraits into one?
// TODO(vardhan): Write a doc about all the Traits templates you need to touch
// when introducing a new mojom type.
// TODO(vardhan): Instead of providing |move_only| & |is_union| template
// parameters, provide a default "typename Enable = void" template parameter,
// and have specializations define their own Enable expressions (similar to how
// ValueTraits is).
template <typename T,
          bool is_move_only = IsMoveOnlyType<T>::value,
          bool is_union =
              IsUnionWrapperType<typename RemoveStructPtr<T>::type>::value,
          typename enable = void>
struct WrapperTraits;

template <typename T>
struct IsHandleType {
  static constexpr bool value = std::is_base_of<zx::object<T>, T>::value ||
                                IsSpecializationOf<zx::object, T>::value;
};
static_assert(IsHandleType<zx::handle>::value,
              "zx::handle should be considered a handle");
static_assert(IsHandleType<zx::channel>::value,
              "zx::channel should be considered a handle.");

// Catch-all for all mojom types not specialized below.
template <typename T>
struct WrapperTraits<T,
                     false,
                     false,
                     typename std::enable_if<!IsHandleType<T>::value>::type> {
  using DataType = T;
};
template <typename H>
struct WrapperTraits<H,
                     true,
                     false,
                     typename std::enable_if<IsHandleType<H>::value>::type> {
  using DataType = WrappedHandle;
};
template <typename I>
struct WrapperTraits<InterfaceRequest<I>, true, false> {
  using DataType = WrappedHandle;
};
template <typename Interface>
struct WrapperTraits<InterfaceHandle<Interface>, true, false> {
  using DataType = Interface_Data;
};
// Unions.
template <typename U>
struct WrapperTraits<StructPtr<U>, true, true> {
  using DataType = typename U::Data_;
};
template <typename U>
struct WrapperTraits<InlinedStructPtr<U>, true, true> {
  using DataType = typename U::Data_;
};
// Structs, arrays, maps.
// TODO(vardhan): Should we only support fidl types? (there are unittests that
// mock types).
template <typename P>
struct WrapperTraits<
    P,
    true,
    false,
    typename std::enable_if<!std::is_void<typename P::Data_>::value>::type> {
  using DataType = typename P::Data_*;
};

template <typename T, typename Enable = void>
struct ValueTraits {
  static bool Equals(const T& a, const T& b) { return a == b; }
};

template <typename T>
struct ValueTraits<T,
                   typename std::enable_if<
                       IsSpecializationOf<Array, T>::value ||
                       IsSpecializationOf<Map, T>::value ||
                       IsSpecializationOf<StructPtr, T>::value ||
                       IsSpecializationOf<InlinedStructPtr, T>::value>::type> {
  static bool Equals(const T& a, const T& b) { return a.Equals(b); }
};

template <typename T>
struct ValueTraits<T, typename std::enable_if<IsHandleType<T>::value>::type> {
  static bool Equals(const T& a, const T& b) {
    return (&a == &b) || (!a && !b);
  }
};

// |InterfaceHandle|s hold channels uniquely, so they can only be equal if
// they're the same object or are both "invalid".
template <typename I>
struct ValueTraits<InterfaceHandle<I>> {
  static bool Equals(const InterfaceHandle<I>& a, const InterfaceHandle<I>& b) {
    return (&a == &b) || (!a.is_valid() && !b.is_valid());
  }
};

// |InterfaceRequest|s hold channels uniquely, so they can only be equal if
// they're the same object or are both "invalid".
template <typename I>
struct ValueTraits<InterfaceRequest<I>> {
  static bool Equals(const InterfaceRequest<I>& a,
                     const InterfaceRequest<I>& b) {
    return (&a == &b) || (!a.is_pending() && !b.is_pending());
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_INTERNAL_H_
