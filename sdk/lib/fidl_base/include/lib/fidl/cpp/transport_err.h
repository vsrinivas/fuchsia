// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSPORT_ERR_H_
#define LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSPORT_ERR_H_

#include <zircon/fidl.h>

namespace fidl {
namespace internal {

enum class TransportErr : fidl_transport_err_t {
  kUnknownMethod = FIDL_TRANSPORT_ERR_UNKNOWN_METHOD,
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSPORT_ERR_H_
