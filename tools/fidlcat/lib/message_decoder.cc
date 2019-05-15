// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message_decoder.h"

#include <src/lib/fxl/logging.h>

#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_object.h"
#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

MessageDecoder::MessageDecoder(const fidl::Message& message, bool output_errors)
    : start_byte_pos_(message.bytes().begin()),
      start_handle_pos_(message.handles().begin()),
      end_byte_pos_(message.bytes().end()),
      end_handle_pos_(message.handles().end()),
      byte_pos_(message.bytes().begin()),
      handle_pos_(message.handles().begin()),
      output_errors_(output_errors) {}

MessageDecoder::MessageDecoder(const MessageDecoder* container,
                               uint64_t num_bytes, uint64_t num_handles)
    : start_byte_pos_(container->byte_pos_),
      start_handle_pos_(container->handle_pos_),
      end_byte_pos_(container->byte_pos_ + num_bytes),
      end_handle_pos_(container->handle_pos_ + num_handles),
      byte_pos_(container->byte_pos_),
      handle_pos_(container->handle_pos_),
      output_errors_(container->output_errors_) {}

std::unique_ptr<Object> MessageDecoder::DecodeMessage(
    const Struct& message_format) {
  std::unique_ptr<Object> result =
      message_format.DecodeObject(this, /*name=*/"", /*offset=*/0,
                                  /*nullable=*/false);
  GotoNextObjectOffset(message_format.size());
  for (size_t i = 0; i < secondary_objects_.size(); ++i) {
    secondary_objects_[i]->DecodeContent(this);
  }
  return result;
}

std::unique_ptr<Field> MessageDecoder::DecodeField(std::string_view name,
                                                   const Type* type) {
  std::unique_ptr<Field> result = type->Decode(this, name, 0);
  GotoNextObjectOffset(type->InlineSize());
  for (size_t i = 0; i < secondary_objects_.size(); ++i) {
    secondary_objects_[i]->DecodeContent(this);
  }
  return result;
}

}  // namespace fidlcat
