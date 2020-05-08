// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ERRORS_H_
#define LIB_FIDL_ASYNC_CPP_ERRORS_H_

namespace fidl {

constexpr char kErrorRequestBufferTooSmall[] = "request buffer too small";
constexpr char kErrorWriteFailed[] = "failed writing to the underlying transport";
constexpr char kErrorChannelUnbound[] = "failed outgoing operation on unbound channel";

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ERRORS_H_
