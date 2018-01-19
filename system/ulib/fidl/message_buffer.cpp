// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/cpp/message_buffer.h>

#include <stdlib.h>

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

} // namespace

MessageBuffer::MessageBuffer(uint32_t bytes_capacity,
                             uint32_t handles_capacity)
    : buffer_(static_cast<uint8_t*>(
          malloc(GetAllocSize(bytes_capacity, handles_capacity)))),
      bytes_capacity_(bytes_capacity),
      handles_capacity_(handles_capacity) {
}

MessageBuffer::~MessageBuffer() {
    free(buffer_);
}

zx_handle_t* MessageBuffer::handles() const {
    return reinterpret_cast<zx_handle_t*>(
        buffer_ + bytes_capacity_ + GetPadding(bytes_capacity_));
}

Message MessageBuffer::CreateEmptyMessage() {
    return Message(BytePart(bytes(), bytes_capacity()),
                   HandlePart(handles(), handles_capacity()));
}

} // namespace fidl
