// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_TRAITS_H_
#define LIB_FIDL_CPP_BINDINGS2_TRAITS_H_

#include <stdint.h>
#include <zx/object.h>

#include <type_traits>

namespace fidl {

// A type trait that indiciates whether the given type is a primitive FIDL
// type.
template<typename T>
struct IsPrimitive : public std::false_type {};

template<> struct IsPrimitive<bool> : public std::true_type {};
template<> struct IsPrimitive<uint8_t> : public std::true_type {};
template<> struct IsPrimitive<uint16_t> : public std::true_type {};
template<> struct IsPrimitive<uint32_t> : public std::true_type {};
template<> struct IsPrimitive<uint64_t> : public std::true_type {};
template<> struct IsPrimitive<int8_t> : public std::true_type {};
template<> struct IsPrimitive<int16_t> : public std::true_type {};
template<> struct IsPrimitive<int32_t> : public std::true_type {};
template<> struct IsPrimitive<int64_t> : public std::true_type {};
template<> struct IsPrimitive<float> : public std::true_type {};
template<> struct IsPrimitive<double> : public std::true_type {};

// A type trait that links the given type to its wire format.
//
// The wire format representation of a type is referred to as the "view" type
// because the bindings return these types to provide type-safe views of the
// underlying message buffer.
//
// Typically, a type that has a wire representation will define a type in its
// scope with the name |View|.
template<typename T, class Enable = void>
struct ViewOf {
  // The wire representaion of |T|.
  using type = typename T::View;
};

template<> struct ViewOf<bool> { using type = bool; };
template<> struct ViewOf<uint8_t> { using type = uint8_t; };
template<> struct ViewOf<uint16_t> { using type = uint16_t; };
template<> struct ViewOf<uint32_t> { using type = uint32_t; };
template<> struct ViewOf<uint64_t> { using type = uint64_t; };
template<> struct ViewOf<int8_t> { using type = int8_t; };
template<> struct ViewOf<int16_t> { using type = int16_t; };
template<> struct ViewOf<int32_t> { using type = int32_t; };
template<> struct ViewOf<int64_t> { using type = int64_t; };
template<> struct ViewOf<float> { using type = float; };
template<> struct ViewOf<double> { using type = double; };

template<typename T>
struct ViewOf<T, typename std::enable_if<
                     std::is_base_of<zx::object_base, T>::value>::type> {
  using type = zx_handle_t;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_TRAITS_H_
