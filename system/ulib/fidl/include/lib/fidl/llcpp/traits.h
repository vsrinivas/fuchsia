// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TRAITS_H_
#define LIB_FIDL_LLCPP_TRAITS_H_

#include <lib/zx/object.h>
#include <stdint.h>
#include <type_traits>
#include <zircon/fidl.h>

// Defines type traits used in the low-level C++ binding.
//
// The contracts of a FIDL type |T| are as follows:
//
// |IsFidlType<T>| resolves to std::true_type.
// |IsFidlMessage<T>| resolves to std::true_type iff |T| is a transactional message.
// |T::MaxNumHandles| is an uint32_t specifying the upper bound on the number of contained handles.
// |T::MaxSize| is an uint32_t specifying the upper bound on the message byte size.
//              It is std::numeric_limits<uint32_t>::max() if |T| is unbounded.
// |T::type| is a fidl_type_t* pointing to the corresponding encoding table, if any.
//

namespace fidl {

// A type trait that indicates whether the given type is a request/response type
// i.e. has a FIDL message header.
template <typename T> struct IsFidlMessage : public std::false_type {};

// Code-gen will explicitly conform the generated FIDL transactional messages to IsFidlMessage.

// A type trait that indicates whether the given type is allowed to appear in
// generated binding APIs and can be encoded/decoded.
// As a start, all handle types are supported.
template <typename T> struct IsFidlType :
    public std::is_base_of<zx::object_base, T> {};

// clang-format off
// Specialize for primitives
template <> struct IsFidlType<bool> : public std::true_type {};
template <> struct IsFidlType<uint8_t> : public std::true_type {};
template <> struct IsFidlType<uint16_t> : public std::true_type {};
template <> struct IsFidlType<uint32_t> : public std::true_type {};
template <> struct IsFidlType<uint64_t> : public std::true_type {};
template <> struct IsFidlType<int8_t> : public std::true_type {};
template <> struct IsFidlType<int16_t> : public std::true_type {};
template <> struct IsFidlType<int32_t> : public std::true_type {};
template <> struct IsFidlType<int64_t> : public std::true_type {};
template <> struct IsFidlType<float> : public std::true_type {};
template <> struct IsFidlType<double> : public std::true_type {};
// clang-format on

// String
class StringView;
template <> struct IsFidlType<StringView> : public std::true_type {};

// Vector (conditional on element)
template <typename E> class VectorView;
template <typename E>
struct IsFidlType<VectorView<E>> : public IsFidlType<E> {};

// Code-gen will explicitly conform the generated FIDL structures to IsFidlType.

}  // namespace fidl

#endif // LIB_FIDL_LLCPP_TRAITS_H_
