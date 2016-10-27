// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_HEADER_VALIDATOR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_HEADER_VALIDATOR_H_

#include <string>

#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/message_validator.h"

namespace fidl {
namespace internal {

class MessageHeaderValidator final : public MessageValidator {
 public:
  ValidationError Validate(const Message* message, std::string* err) override;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_HEADER_VALIDATOR_H_
