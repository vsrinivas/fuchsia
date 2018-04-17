// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message_buffer.h>
#include <zircon/assert.h>

#include <stdlib.h>

namespace fidl {
namespace {

uint64_t AddPadding(uint32_t offset) {
    constexpr uint32_t kMask = alignof(zx_handle_t) - 1;
    // Cast before addition to avoid overflow.
    return static_cast<uint64_t>(offset) + static_cast<uint64_t>(offset & kMask);
}

size_t GetAllocSize(uint32_t bytes_capacity, uint32_t handles_capacity) {
    return AddPadding(bytes_capacity) + sizeof(zx_handle_t) * handles_capacity;
}

} // namespace

MessageBuffer::MessageBuffer(uint32_t bytes_capacity,
                             uint32_t handles_capacity)
    : buffer_(static_cast<uint8_t*>(malloc(GetAllocSize(bytes_capacity, handles_capacity)))),
      bytes_capacity_(bytes_capacity),
      handles_capacity_(handles_capacity) {
    ZX_ASSERT_MSG(buffer_, "malloc returned NULL in MessageBuffer::MessageBuffer()");
}

MessageBuffer::~MessageBuffer() {
    free(buffer_);
}

zx_handle_t* MessageBuffer::handles() const {
    return reinterpret_cast<zx_handle_t*>(buffer_ + AddPadding(bytes_capacity_));
}

Message MessageBuffer::CreateEmptyMessage() {
    return Message(BytePart(bytes(), bytes_capacity()),
                   HandlePart(handles(), handles_capacity()));
}

} // namespace fidl
