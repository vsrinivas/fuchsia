// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_VALIDATION_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_VALIDATION_H_

#include <string>

#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {
namespace internal {

// Validates that the message is a request which doesn't expect a response.
ValidationError ValidateMessageIsRequestWithoutResponse(const Message* message,
                                                        std::string* err);
// Validates that the message is a request expecting a response.
ValidationError ValidateMessageIsRequestExpectingResponse(
    const Message* message,
    std::string* err);
// Validates that the message is a response.
ValidationError ValidateMessageIsResponse(const Message* message,
                                          std::string* err);

// Validates that the message payload is a valid struct of type ParamsType.
template <typename ParamsType>
ValidationError ValidateMessagePayload(const Message* message,
                                       std::string* err) {
  BoundsChecker bounds_checker(message->payload(), message->payload_num_bytes(),
                               message->handles()->size());
  return ParamsType::Validate(message->payload(), &bounds_checker, err);
}

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_VALIDATION_H_
