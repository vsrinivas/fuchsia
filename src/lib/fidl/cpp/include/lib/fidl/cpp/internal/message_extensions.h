// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_

#include <lib/fidl/llcpp/message.h>
#include <lib/stdcompat/span.h>

namespace fidl {
namespace internal {

// Given a transactional message, extracts the parts following the transaction
// header, and re-package that as another |fidl::IncomingMessage|. This is
// useful for decoding the request/response payload of a method.
::fidl::IncomingMessage SkipTransactionHeader(::fidl::IncomingMessage message);

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_
