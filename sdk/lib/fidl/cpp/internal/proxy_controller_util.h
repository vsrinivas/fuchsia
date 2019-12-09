// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_UTIL_H_
#define LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_UTIL_H_

#include <lib/fidl/cpp/message.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// When the FIDL bindings are configured to write wire format v1, the Message
// bytes and coding table passed to ProxyController::Send are not in a format
// that can be validated using fidl_validate. To get around this, we call
// fidl_transform to write the message bytes into the old format and then call
// fidl_validate on it, which also serves to validate the message bytes in the v1
// format.
// TODO(fxb/42311) Remove or rewrite using fidl_transform_with_callback
zx_status_t ValidateV1Bytes(const fidl_type_t* type, const Message& message, const char* error_msg);

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_UTIL_H_
