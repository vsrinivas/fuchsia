// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_UNKNOWN_INTERACTIONS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_UNKNOWN_INTERACTIONS_H_

#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fidl/cpp/wire/unknown_interactions.h>

namespace fidl::internal {

// Builds and sends an unknown interaction reply with the given value for the
// Driver transport. This is used as part of the
// |UnknownMethodHandlerEntry| for protocols which use the Driver
// transport. For protocols using Channel transport, see
// |SendChannelUnknownMethodReply|.
void SendDriverUnknownMethodReply(UnknownMethodReply reply, ::fidl::Transaction* txn);

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_UNKNOWN_INTERACTIONS_H_
