// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message_buffer.h>
#include <stdlib.h>
#include <zircon/assert.h>

namespace fidl {
namespace {

template <typename HandleType>
uint64_t AddPadding(uint32_t offset) {
  constexpr uint32_t kMask = alignof(HandleType) - 1;
  // Cast before addition to avoid overflow.
  return static_cast<uint64_t>(offset) + static_cast<uint64_t>(offset & kMask);
}

template <typename HandleType>
size_t GetAllocSize(uint32_t bytes_capacity, uint32_t handles_capacity) {
  return AddPadding<HandleType>(bytes_capacity) + sizeof(HandleType) * handles_capacity;
}

}  // namespace

OutgoingMessageBuffer::OutgoingMessageBuffer(uint32_t bytes_capacity, uint32_t handles_capacity)
    : buffer_(static_cast<uint8_t*>(
          malloc(GetAllocSize<zx_handle_disposition_t>(bytes_capacity, handles_capacity)))),
      bytes_capacity_(bytes_capacity),
      handles_capacity_(handles_capacity) {
  ZX_ASSERT_MSG(buffer_, "malloc returned NULL in OutgoingMessageBuffer::OutgoingMessageBuffer()");
}

OutgoingMessageBuffer::~OutgoingMessageBuffer() { free(buffer_); }

zx_handle_disposition_t* OutgoingMessageBuffer::handles() const {
  return reinterpret_cast<zx_handle_disposition_t*>(
      buffer_ + AddPadding<zx_handle_disposition_t>(bytes_capacity_));
}

HLCPPOutgoingMessage OutgoingMessageBuffer::CreateEmptyOutgoingMessage() {
  return HLCPPOutgoingMessage(BytePart(bytes(), bytes_capacity()),
                              HandleDispositionPart(handles(), handles_capacity()));
}

Builder OutgoingMessageBuffer::CreateBuilder() { return Builder(bytes(), bytes_capacity()); }

IncomingMessageBuffer::IncomingMessageBuffer(uint32_t bytes_capacity, uint32_t handles_capacity)
    : buffer_(static_cast<uint8_t*>(
          malloc(GetAllocSize<zx_handle_info_t>(bytes_capacity, handles_capacity)))),
      bytes_capacity_(bytes_capacity),
      handles_capacity_(handles_capacity) {
  ZX_ASSERT_MSG(buffer_, "malloc returned NULL in IncomingMessageBuffer::IncomingMessageBuffer()");
}

IncomingMessageBuffer::~IncomingMessageBuffer() { free(buffer_); }

zx_handle_info_t* IncomingMessageBuffer::handles() const {
  return reinterpret_cast<zx_handle_info_t*>(buffer_ +
                                             AddPadding<zx_handle_info_t>(bytes_capacity_));
}

HLCPPIncomingMessage IncomingMessageBuffer::CreateEmptyIncomingMessage() {
  return HLCPPIncomingMessage(BytePart(bytes(), bytes_capacity()),
                              HandleInfoPart(handles(), handles_capacity()));
}

}  // namespace fidl
