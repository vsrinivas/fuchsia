// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"

#include <string>

#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"
#include "lib/fidl/cpp/bindings/internal/message_validation.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/internal/validation_util.h"

namespace fidl {
namespace internal {
namespace {

ValidationError ValidateMessageHeader(const MessageHeader* header,
                                      std::string* err) {
  // NOTE: Our goal is to preserve support for future extension of the message
  // header. If we encounter fields we do not understand, we must ignore them.
  // Extra validation of the struct header:
  if (header->version == 0) {
    if (header->num_bytes != sizeof(MessageHeader)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
          << "message header size is incorrect";
      return ValidationError::UNEXPECTED_STRUCT_HEADER;
    }
  } else if (header->version == 1) {
    if (header->num_bytes != sizeof(MessageHeaderWithRequestID)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
          << "message header (version = 1) size is incorrect";
      return ValidationError::UNEXPECTED_STRUCT_HEADER;
    }
  } else if (header->version > 1) {
    if (header->num_bytes < sizeof(MessageHeaderWithRequestID)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
          << "message header (version > 1) size is too small";
      return ValidationError::UNEXPECTED_STRUCT_HEADER;
    }
  }

  // Validate flags (allow unknown bits):

  // These flags require a RequestID.
  if (header->version < 1 && ((header->flags & kMessageExpectsResponse) ||
                              (header->flags & kMessageIsResponse))) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message header associates itself with a response but does not "
           "contain a request id";
    return ValidationError::MESSAGE_HEADER_MISSING_REQUEST_ID;
  }

  // These flags are mutually exclusive.
  if ((header->flags & kMessageExpectsResponse) &&
      (header->flags & kMessageIsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message header cannot indicate itself as a response while also "
           "expecting a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }

  return ValidationError::NONE;
}

}  // namespace

ValidationError MessageHeaderValidator::Validate(const Message* message,
                                                 std::string* err) {
  // Pass 0 as number of handles because we don't expect any in the header, even
  // if |message| contains handles.
  BoundsChecker bounds_checker(message->data(), message->data_num_bytes(), 0);

  ValidationError result =
      ValidateStructHeaderAndClaimMemory(message->data(), &bounds_checker, err);
  if (result != ValidationError::NONE)
    return result;

  return ValidateMessageHeader(message->header(), err);
}

}  // namespace internal
}  // namespace fidl
