// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ERRORS_H_
#define LIB_FIDL_LLCPP_ERRORS_H_

namespace fidl {

constexpr char kErrorRequestBufferTooSmall[] = "request buffer too small";
constexpr char kErrorInvalidHeader[] = "invalid header";
constexpr char kErrorReadFailed[] = "failed reading from the underlying transport";
constexpr char kErrorWriteFailed[] = "failed writing to the underlying transport";
constexpr char kErrorChannelUnbound[] = "failed outgoing operation on unbound channel";
constexpr char kErrorWaitOneFailed[] = "zx_channel_wait_one failed";

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ERRORS_H_
