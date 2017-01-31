// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_buffer.h"

namespace bluetooth {
namespace common {

DynamicByteBuffer::DynamicByteBuffer() : buffer_size_(0u) {}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size)
    : buffer_size_(buffer_size) {
  FTL_DCHECK(buffer_size_) << "|buffer_size| must be non-zero";
  buffer_ = std::make_unique<uint8_t[]>(buffer_size);

  // TODO(armansito): For now this is dumb but we should properly handle the
  // case when we're out of memory.
  FTL_DCHECK(buffer_.get()) << "Failed to allocate buffer";
}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size,
                                     std::unique_ptr<uint8_t[]> buffer)
    : buffer_size_(buffer_size), buffer_(std::move(buffer)) {
  FTL_DCHECK(buffer_size_) << "|buffer_size| must be non-zero";
  FTL_DCHECK(buffer_.get()) << "|buffer| cannot be nullptr";
}

DynamicByteBuffer::DynamicByteBuffer(DynamicByteBuffer&& other) {
  buffer_size_ = other.buffer_size_;
  other.buffer_size_ = 0u;
  buffer_ = std::move(other.buffer_);
}

DynamicByteBuffer& DynamicByteBuffer::operator=(DynamicByteBuffer&& other) {
  buffer_size_ = other.buffer_size_;
  other.buffer_size_ = 0u;
  buffer_ = std::move(other.buffer_);
  return *this;
}

const uint8_t* DynamicByteBuffer::GetData() const {
  return buffer_.get();
}

uint8_t* DynamicByteBuffer::GetMutableData() {
  return buffer_.get();
}

size_t DynamicByteBuffer::GetSize() const {
  return buffer_size_;
}

void DynamicByteBuffer::SetToZeros() {
  memset(buffer_.get(), 0, buffer_size_);
}

std::unique_ptr<uint8_t[]> DynamicByteBuffer::TransferContents() {
  auto moved = std::move(buffer_);
  buffer_size_ = 0u;
  return moved;
}

ByteBuffer::const_iterator DynamicByteBuffer::cbegin() const {
  return buffer_.get();
}

ByteBuffer::const_iterator DynamicByteBuffer::cend() const {
  return buffer_.get() + buffer_size_;
}

BufferView::BufferView(uint8_t* bytes, size_t size)
    : size_(size), bytes_(bytes) {
  FTL_DCHECK(bytes_);
  FTL_DCHECK(size_);
}

const uint8_t* BufferView::GetData() const {
  return bytes_;
}

uint8_t* BufferView::GetMutableData() {
  return bytes_;
}

size_t BufferView::GetSize() const {
  return size_;
}

void BufferView::SetToZeros() {
  memset(bytes_, 0, size_);
}

std::unique_ptr<uint8_t[]> BufferView::TransferContents() {
  auto buffer = std::make_unique<uint8_t[]>(size_);
  memcpy(buffer.get(), bytes_, size_);
  return buffer;
}

ByteBuffer::const_iterator BufferView::cbegin() const {
  return bytes_;
}

ByteBuffer::const_iterator BufferView::cend() const {
  return bytes_ + size_;
}

}  // namespace common
}  // namespace bluetooth
