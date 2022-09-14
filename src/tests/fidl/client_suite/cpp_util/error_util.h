// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CLIENT_SUITE_CPP_UTIL_ERROR_UTIL_H_
#define SRC_TESTS_FIDL_CLIENT_SUITE_CPP_UTIL_ERROR_UTIL_H_

#include <fidl/fidl.clientsuite/cpp/fidl.h>
#include <lib/fidl/cpp/wire/status.h>

namespace clienttest_util {

fidl_clientsuite::FidlErrorKind ClassifyError(const fidl::Status& status) {
  ZX_ASSERT(!status.ok());
  switch (status.reason()) {
    case fidl::Reason::kUnbind:
    case fidl::Reason::kClose:
    case fidl::Reason::kDispatcherError:
    case fidl::Reason::kTransportError:
    case fidl::Reason::kEncodeError:
      return fidl_clientsuite::FidlErrorKind::kOtherError;
    case fidl::Reason::kUnexpectedMessage:
      return fidl_clientsuite::FidlErrorKind::kUnexpectedMessage;
    case fidl::Reason::kPeerClosed:
      return fidl_clientsuite::FidlErrorKind::kChannelPeerClosed;
    case fidl::Reason::kDecodeError:
      return fidl_clientsuite::FidlErrorKind::kDecodingError;
    case fidl::Reason::kUnknownMethod:
      return fidl_clientsuite::FidlErrorKind::kUnknownMethod;
    default:
      auto description = status.FormatDescription();
      ZX_PANIC("Status had an unspported reason: %s", description.c_str());
  }
}

}  // namespace clienttest_util

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_CPP_UTIL_ERROR_UTIL_H_
