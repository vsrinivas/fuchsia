// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_

#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_types.h>
#include <lib/fitx/result.h>

#include <cstdint>

// # Natural domain objects
//
// This header contains forward definitions that are part of natural domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

// |Error| is a type alias for when the result of an operation is an error.
using Error = Result;

namespace internal {

// |DesignatedInitializationProxy<FidlStruct>| is the name of the aggregate
// type associated with this struct in natural domain objects, to support
// designated initialization. Natural structs would support implicitly casting
// from these proxy types, hence permitting syntax like the following:
//
//     // The inner brace constructs a designated initialization proxy.
//     MyStruct{{ .a = 1, .b = "foo" }};
//
// The name of this type should not be spelled out directly in user code.
// Its only purpose is to provide initialization syntax sugar.
template <typename FidlStruct>
struct DesignatedInitializationProxy;

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
