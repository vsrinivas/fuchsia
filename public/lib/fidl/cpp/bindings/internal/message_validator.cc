// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/message_validator.h"

namespace fidl {
namespace internal {

ValidationError PassThroughValidator::Validate(const Message* message,
                                               std::string* err) {
  return ValidationError::NONE;
}

ValidationError RunValidatorsOnMessage(const MessageValidatorList& validators,
                                       const Message* message,
                                       std::string* err) {
  for (const auto& validator : validators) {
    auto result = validator->Validate(message, err);
    if (result != ValidationError::NONE)
      return result;
  }

  return ValidationError::NONE;
}

}  // namespace internal
}  // namespace fidl
