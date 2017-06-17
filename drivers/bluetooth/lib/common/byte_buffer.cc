// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_buffer.h"

namespace bluetooth {
namespace common {

std::unique_ptr<uint8_t[]> ByteBuffer::CopyContents() const {
  if (!size()) return nullptr;

  auto buffer = std::make_unique<uint8_t[]>(size());
  memcpy(buffer.get(), data(), size());
  return buffer;
}

ftl::StringView ByteBuffer::AsString() const {
  return ftl::StringView(reinterpret_cast<const char*>(data()), size());
}

std::string ByteBuffer::ToString() const {
  return AsString().ToString();
}

DynamicByteBuffer::DynamicByteBuffer() : buffer_size_(0u) {}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size) : buffer_size_(buffer_size) {
  FTL_DCHECK(buffer_size_) << "|buffer_size| must be non-zero";
  buffer_ = std::make_unique<uint8_t[]>(buffer_size);

  // TODO(armansito): For now this is dumb but we should properly handle the
  // case when we're out of memory.
  FTL_DCHECK(buffer_.get()) << "Failed to allocate buffer";
}

DynamicByteBuffer::DynamicByteBuffer(const ByteBuffer& buffer)
    : buffer_size_(buffer.size()), buffer_(buffer.CopyContents()) {
  FTL_DCHECK(!buffer_size_ || buffer_.get())
      << "|buffer| cannot be nullptr when |buffer_size| is non-zero";
}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size, std::unique_ptr<uint8_t[]> buffer)
    : buffer_size_(buffer_size), buffer_(std::move(buffer)) {
  FTL_DCHECK(!buffer_size_ || buffer_.get())
      << "|buffer| cannot be nullptr when |buffer_size| is non-zero";
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

const uint8_t* DynamicByteBuffer::data() const {
  return buffer_.get();
}

uint8_t* DynamicByteBuffer::mutable_data() {
  return buffer_.get();
}

size_t DynamicByteBuffer::size() const {
  return buffer_size_;
}

void DynamicByteBuffer::SetToZeros() {
  memset(buffer_.get(), 0, buffer_size_);
}

ByteBuffer::const_iterator DynamicByteBuffer::cbegin() const {
  return buffer_.get();
}

ByteBuffer::const_iterator DynamicByteBuffer::cend() const {
  return buffer_.get() + buffer_size_;
}

BufferView::BufferView(const ByteBuffer& buffer) {
  size_ = buffer.size();
  bytes_ = buffer.data();
}

BufferView::BufferView(const ftl::StringView& string) {
  size_ = string.size();
  bytes_ = reinterpret_cast<const uint8_t*>(string.data());
}

BufferView::BufferView(const uint8_t* bytes, size_t size) : size_(size), bytes_(bytes) {
  // If |size| non-zero then |bytes| cannot be nullptr.
  FTL_DCHECK(!size_ || bytes_) << "|bytes_| cannot be nullptr if |size_| > 0";
}

BufferView::BufferView() : size_(0u), bytes_(nullptr) {}

const uint8_t* BufferView::data() const {
  return bytes_;
}

size_t BufferView::size() const {
  return size_;
}

ByteBuffer::const_iterator BufferView::cbegin() const {
  return bytes_;
}

ByteBuffer::const_iterator BufferView::cend() const {
  return bytes_ + size_;
}

MutableBufferView::MutableBufferView(MutableByteBuffer* buffer) {
  FTL_DCHECK(buffer);
  size_ = buffer->size();
  bytes_ = buffer->mutable_data();
}

MutableBufferView::MutableBufferView(uint8_t* bytes, size_t size) : size_(size), bytes_(bytes) {
  // If |size| non-zero then |bytes| cannot be nullptr.
  FTL_DCHECK(!size_ || bytes_) << "|bytes_| cannot be nullptr if |size_| > 0";
}

MutableBufferView::MutableBufferView() : size_(0u), bytes_(nullptr) {}

const uint8_t* MutableBufferView::data() const {
  return bytes_;
}

size_t MutableBufferView::size() const {
  return size_;
}

ByteBuffer::const_iterator MutableBufferView::cbegin() const {
  return bytes_;
}

ByteBuffer::const_iterator MutableBufferView::cend() const {
  return bytes_ + size_;
}

uint8_t* MutableBufferView::mutable_data() {
  return bytes_;
}

void MutableBufferView::SetToZeros() {
  memset(bytes_, 0, size_);
}

}  // namespace common
}  // namespace bluetooth
