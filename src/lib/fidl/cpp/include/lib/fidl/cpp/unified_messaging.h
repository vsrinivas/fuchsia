// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_

#include <lib/fidl/cpp/internal/natural_client_base.h>

#include <cstdint>

namespace fidl {
namespace internal {

// |NaturalClientImpl| implements methods for making synchronous and
// asynchronous FIDL calls with natural types.
//
// All specializations of |NaturalClientImpl| should inherit from
// |fidl::internal::NaturalClientBase|.
// TODO(fxbug.dev/60240): Generate this interface.
template <typename Protocol>
class NaturalClientImpl;

}  // namespace internal

// |AsyncEventHandler| is used by asynchronous clients to handle events
// using natural types. It also adds a callback for handling errors.
// TODO(fxbug.dev/60240): Generate this interface.
template <typename Protocol>
class AsyncEventHandler;

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNIFIED_MESSAGING_H_
