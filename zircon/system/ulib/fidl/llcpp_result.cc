// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/result.h>
#include <lib/fit/nullable.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/compiler.h>

#include <array>
#include <iostream>
#include <string>

namespace fidl {

namespace internal {

const char* const kErrorInvalidHeader = "invalid header";
const char* const kErrorUnknownTxId = "unknown txid";
const char* const kErrorUnknownOrdinal = "unknown ordinal";
const char* const kErrorTransport = "underlying transport I/O error";
const char* const kErrorChannelUnbound = "failed outgoing operation on unbound channel";
const char* const kErrorWaitOneFailed = "zx_channel_wait_one failed";
const char* const kCallerAllocatedBufferTooSmall =
    "buffer provided to caller-allocating flavor is too small";

}  // namespace internal

namespace {

// A buffer of 256 bytes is sufficient for all tested results.
// If the description exceeds this length at runtime,
// the output will be truncated.
// We can increase the size if necessary.
using ResultFormattingBuffer = std::array<char, 256>;

}  // namespace

[[nodiscard]] std::string Result::FormatDescription() const {
  ResultFormattingBuffer buf;
  size_t length = FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ false);
  return std::string(buf.begin(), length);
}

[[nodiscard]] const char* Result::lossy_description() const {
  // If an error string was explicitly specified, use that.
  if (error_) {
    return error_;
  }
  // Otherwise, derive an error from |reason_|.
  return reason_description();
}

[[nodiscard]] const char* Result::reason_description() const {
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

size_t Result::FormatImpl(char* destination, size_t length, bool from_unbind_info) const {
  // We use |snprintf| since it minimizes allocation and is faster
  // than output streams.
  if (!from_unbind_info && status_ == ZX_OK && reason_ == internal::kUninitializedReason) {
    return snprintf(destination, length, "FIDL success");
  }

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
  return snprintf(destination, length, "%s due to %s, %s: %s (%d)%s%s", prelude,
                  reason_description(), status_meaning, status_string(), status_, detail_prefix,
                  detail);
#else
  return snprintf(destination, length, "%s due to %s, %s: %d%s%s", prelude, reason_description(),
                  status_meaning, status_, detail_prefix, detail);
#endif  // __Fuchsia__
}

std::ostream& operator<<(std::ostream& ostream, const Result& result) {
  ResultFormattingBuffer buf;
  size_t length = result.FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ false);
  ostream << cpp17::string_view(buf.begin(), length);
  return ostream;
}

[[nodiscard]] std::string UnbindInfo::FormatDescription() const {
  ResultFormattingBuffer buf;
  size_t length = FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ true);
  return std::string(buf.begin(), length);
}

std::ostream& operator<<(std::ostream& ostream, const UnbindInfo& info) {
  ResultFormattingBuffer buf;
  size_t length = info.Result::FormatImpl(buf.begin(), sizeof(buf), /* from_unbind_info */ true);
  ostream << cpp17::string_view(buf.begin(), length);
  return ostream;
}

}  // namespace fidl
