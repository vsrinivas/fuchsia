// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_messaging.h>

namespace fidl::internal {

::fit::result<::fidl::Error> VerifyBodyIsAbsent(const ::fidl::EncodedMessage& body) {
  if (unlikely(!body.bytes().empty())) {
    return ::fit::error(
        ::fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNotAllBytesConsumed));
  }
  if (unlikely(body.handle_actual() > 0)) {
    return ::fit::error(
        ::fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorNotAllHandlesConsumed));
  }
  return ::fit::ok();
}

::fit::result<::fidl::Error> DecodeTransactionalMessageWithoutBody(
    ::fidl::IncomingHeaderAndMessage message) {
  if (!message.ok()) {
    return ::fit::error(message.error());
  }
  const fidl_message_header& header = *message.header();
  auto metadata = ::fidl::WireFormatMetadata::FromTransactionalHeader(header);
  fidl::EncodedMessage body_message = std::move(message).SkipTransactionHeader();
  if (unlikely(!metadata.is_valid())) {
    return ::fit::error(
        ::fidl::Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorInvalidWireFormatMetadata));
  }
  return VerifyBodyIsAbsent(body_message);
}

}  // namespace fidl::internal
