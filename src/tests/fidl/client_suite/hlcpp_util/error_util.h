// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CLIENT_SUITE_HLCPP_UTIL_ERROR_UTIL_H_
#define SRC_TESTS_FIDL_CLIENT_SUITE_HLCPP_UTIL_ERROR_UTIL_H_

#include <fidl/clientsuite/cpp/fidl.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <iostream>

namespace clienttest_util {

fidl::clientsuite::FidlErrorKind ClassifyError(zx_status_t status) {
  ZX_ASSERT(status != ZX_OK);
  switch (status) {
    case ZX_ERR_PEER_CLOSED:
      return fidl::clientsuite::FidlErrorKind::CHANNEL_PEER_CLOSED;
    case ZX_ERR_INVALID_ARGS:
      return fidl::clientsuite::FidlErrorKind::DECODING_ERROR;
    case ZX_ERR_NOT_SUPPORTED:
    case ZX_ERR_NOT_FOUND:
      return fidl::clientsuite::FidlErrorKind::UNEXPECTED_MESSAGE;
    default:
      return fidl::clientsuite::FidlErrorKind::OTHER_ERROR;
  }
}

}  // namespace clienttest_util

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_HLCPP_UTIL_ERROR_UTIL_H_
