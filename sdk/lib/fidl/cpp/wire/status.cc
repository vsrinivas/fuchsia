// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/status.h>
#include <lib/fit/nullable.h>
#include <zircon/compiler.h>

#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace fidl {

namespace internal {

const char* const kErrorInvalidHeader = "invalid header";
const char* const kErrorUnknownTxId = "unknown txid";
const char* const kErrorUnknownOrdinal = "unknown ordinal";
const char* const kErrorTransport = "underlying transport I/O error";
const char* const kErrorChannelUnbound = "failed outgoing operation on unbound channel";
const char* const kErrorWaitOneFailed = "zx_channel_wait_one failed";
const char* const kErrorSyncEventBufferTooSmall =
    "received a larger message than allowed by the events";
const char* const kErrorSyncEventUnhandledTransitionalEvent = "unhandled transitional event";
const char* const kCallerAllocatedBufferTooSmall =
    "buffer provided to caller-allocating flavor is too small";
const char* const kUnknownMethod = "server did not recognize this method";
const char* const kUnsupportedTransportError =
    "server sent a transport_err value that is not supported";

}  // namespace internal

namespace {

// A buffer of 256 bytes is sufficient for all tested results.
// If the description exceeds this length at runtime,
// the output will be truncated.
// We can increase the size if necessary.
using StatusFormattingBuffer = std::array<char, 256>;

}  // namespace

[[nodiscard]] std::string Status::FormatDescription() const {
  StatusFormattingBuffer buf;
  size_t length = FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ false);
  return std::string(buf.begin(), length);
}

[[nodiscard]] const char* Status::lossy_description() const {
  // If an error string was explicitly specified, use that.
  if (error_) {
    return error_;
  }
  // Otherwise, derive an error from |reason_|.
  return reason_description();
}

[[nodiscard]] const char* Status::reason_description() const {
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
    case Reason::kUnknownMethod:
      return "unknown interaction";
  }
}

size_t Status::FormatImpl(char* destination, size_t length, bool from_unbind_info) const {
  ZX_ASSERT(length > 0);
  int num_would_write = 0;

  // We use |snprintf| since it minimizes allocation and is faster
  // than output streams.
  if (!from_unbind_info && status_ == ZX_OK && reason_ == internal::kUninitializedReason) {
    num_would_write = snprintf(destination, length, "FIDL success");
  } else {
    const char* prelude = from_unbind_info ? "FIDL endpoint was unbound" : "FIDL operation failed";

    const char* status_meaning = [&] {
      switch (reason_) {
        // This reason may only appear in an |UnbindInfo|.
        case Reason::kClose:
          ZX_DEBUG_ASSERT(from_unbind_info);
          return "status of sending epitaph";
        case Reason::kPeerClosed:
          if (status_ != ZX_ERR_PEER_CLOSED) {
            return "epitaph";
          }
          break;
        default:
          break;
      }
      return "status";
    }();

    const char* detail_prefix = error_ ? ", detail: " : "";
    const char* detail = error_ ? error_ : "";

#ifdef __Fuchsia__
    num_would_write = snprintf(destination, length, "%s due to %s, %s: %s (%d)%s%s", prelude,
                               reason_description(), status_meaning, status_string(), status_,
                               detail_prefix, detail);
#else
    num_would_write =
        snprintf(destination, length, "%s due to %s, %s: %d%s%s", prelude, reason_description(),
                 status_meaning, status_, detail_prefix, detail);
#endif  // __Fuchsia__
  }

  ZX_ASSERT(num_would_write > 0);
  return static_cast<size_t>(num_would_write) >= length ? length - 1 : num_would_write;
}

std::ostream& operator<<(std::ostream& ostream, const Status& result) {
  StatusFormattingBuffer buf;
  size_t length = result.FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ false);
  ostream << std::string_view(buf.begin(), length);
  return ostream;
}

[[nodiscard]] std::string UnbindInfo::FormatDescription() const {
  StatusFormattingBuffer buf;
  size_t length = FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ true);
  return std::string(buf.begin(), length);
}

std::ostream& operator<<(std::ostream& ostream, const UnbindInfo& info) {
  StatusFormattingBuffer buf;
  size_t length = info.Status::FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ true);
  ostream << std::string_view(buf.begin(), length);
  return ostream;
}

size_t fidl::internal::DisplayError<fidl::Status>::Format(const fidl::Status& value,
                                                          char* destination, size_t capacity) {
  return value.FormatImpl(destination, capacity, false);
}

}  // namespace fidl
