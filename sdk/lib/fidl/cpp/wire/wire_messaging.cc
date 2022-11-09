// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_messaging.h>

namespace fidl::internal {

namespace {

// Verifies that |body| has zero bytes and no handles.
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

}  // namespace

::fit::result<::fidl::Error> DecodeTransactionalMessageWithoutBody(
    ::fidl::IncomingHeaderAndMessage message) {
  if (!message.ok()) {
    return ::fit::error(message.error());
  }
  const fidl_message_header& header = *message.header();
  auto metadata = ::fidl::WireFormatMetadata::FromTransactionalHeader(header);
  fidl::EncodedMessage body_message = std::move(message).SkipTransactionHeader();
  return DecodeTransactionalMessageWithoutBody(body_message, metadata);
}

::fit::result<::fidl::Error> DecodeTransactionalMessageWithoutBody(
    const ::fidl::EncodedMessage& message, ::fidl::WireFormatMetadata metadata) {
  if (fidl::Status status = EnsureSupportedWireFormat(metadata); !status.ok()) {
    return fit::error(status);
  }
  return VerifyBodyIsAbsent(message);
}

}  // namespace fidl::internal
