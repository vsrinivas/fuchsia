// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/message_validation.h"

#include <string>

#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {
namespace internal {

ValidationError ValidateMessageIsRequestWithoutResponse(const Message* message,
                                                        std::string* err) {
  if (message->has_flag(kMessageIsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message should be a request, not a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }
  if (message->has_flag(kMessageExpectsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message should not expect a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }
  return ValidationError::NONE;
}

ValidationError ValidateMessageIsRequestExpectingResponse(
    const Message* message,
    std::string* err) {
  if (message->has_flag(kMessageIsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message should be a request, not a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }
  if (!message->has_flag(kMessageExpectsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
        << "message should expect a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }
  return ValidationError::NONE;
}

ValidationError ValidateMessageIsResponse(const Message* message,
                                          std::string* err) {
  if (message->has_flag(kMessageExpectsResponse) ||
      !message->has_flag(kMessageIsResponse)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "message should be a response";
    return ValidationError::MESSAGE_HEADER_INVALID_FLAGS;
  }
  return ValidationError::NONE;
}

}  // namespace internal
}  // namespace fidl
