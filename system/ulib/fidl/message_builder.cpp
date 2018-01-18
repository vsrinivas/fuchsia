// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/cpp/message_builder.h>

#include <stdlib.h>
#include <stdio.h>

namespace fidl {
namespace {

uint32_t GetPadding(uint32_t offset) {
    constexpr uint32_t kMask = alignof(zx_handle_t) - 1;
    return offset & kMask;
}

size_t GetAllocSize(uint32_t bytes_capacity, uint32_t handles_capacity) {
    return bytes_capacity + GetPadding(bytes_capacity) +
        sizeof(zx_handle_t) * handles_capacity;
}

zx_handle_t* GetHandleBuffer(uint8_t* bytes, uint32_t bytes_capacity) {
    return reinterpret_cast<zx_handle_t*>(
        bytes + bytes_capacity + GetPadding(bytes_capacity));
}

} // namespace

MessageBuilder::MessageBuilder(const fidl_type_t* type,
                               uint32_t bytes_capacity,
                               uint32_t handles_capacity)
    : Builder(malloc(GetAllocSize(bytes_capacity, handles_capacity)),
              bytes_capacity),
      type_(type),
      handles_capacity_(handles_capacity) {
    New<fidl_message_header_t>();
}

MessageBuilder::~MessageBuilder() {
    free(buffer());
}

zx_status_t MessageBuilder::Encode(Message* message_out,
                                   const char** error_msg_out) {
    zx_handle_t* handles = GetHandleBuffer(buffer(), capacity());
    *message_out = Message(Finalize(), HandlePart(handles, handles_capacity_));
    return message_out->Encode(type_, error_msg_out);
}

} // namespace fidl
