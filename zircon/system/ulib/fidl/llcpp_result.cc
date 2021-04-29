// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/result.h>

namespace fidl {

namespace internal {

const char* const kErrorInvalidHeader = "invalid header";
const char* const kErrorUnknownTxId = "unknown txid";
const char* const kErrorUnknownOrdinal = "unknown ordinal";
const char* const kErrorTransport = "underlying transport I/O error";
const char* const kErrorChannelUnbound = "failed outgoing operation on unbound channel";
const char* const kErrorWaitOneFailed = "zx_channel_wait_one failed";

}  // namespace internal

[[nodiscard]] const char* Result::error_message() const {
  // If an error string was explicitly specified, use that.
  if (error_) {
    return error_;
  }
  // Otherwise, derive an error from |reason_|.
  // The error descriptions are quite terse to save binary size.
  switch (reason_) {
    case internal::kUninitializedReason:
      return nullptr;
    case Reason::kUnbind:
      return "user initiated unbind";
    case Reason::kClose:
      return "(server) user initiated close with epitaph";
    case Reason::kPeerClosed:
      return "peer closed";
    case Reason::kDispatcherError:
      return "dispatcher error";
    case Reason::kTransportError:
      return internal::kErrorTransport;
    case Reason::kEncodeError:
      return "encode error";
    case Reason::kDecodeError:
      return "decode error";
    case Reason::kUnexpectedMessage:
      return "unexpected message";
  }
}

}  // namespace fidl
