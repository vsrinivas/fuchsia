// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message_builder.h>

#include <stdlib.h>
#include <stdio.h>

namespace fidl {

MessageBuilder::MessageBuilder(const fidl_type_t* type,
                               uint32_t bytes_capacity,
                               uint32_t handles_capacity)
    : type_(type),
      buffer_(bytes_capacity, handles_capacity) {
    Reset();
}

MessageBuilder::~MessageBuilder() = default;

zx_status_t MessageBuilder::Encode(Message* message_out,
                                   const char** error_msg_out) {
    *message_out = Message(Finalize(),
                           HandlePart(buffer_.handles(),
                                      buffer_.handles_capacity()));
    return message_out->Encode(type_, error_msg_out);
}

void MessageBuilder::Reset() {
    Builder::Reset(buffer_.bytes(), buffer_.bytes_capacity());
    New<fidl_message_header_t>();
}

} // namespace fidl
