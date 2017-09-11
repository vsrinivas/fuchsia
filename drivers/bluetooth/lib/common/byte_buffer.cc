// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_buffer.h"

namespace bluetooth {
namespace common {

size_t ByteBuffer::Copy(MutableByteBuffer* out_buffer, size_t pos, size_t size) const {
  FXL_DCHECK(out_buffer);
  FXL_DCHECK(pos <= this->size()) << "|pos| contains an invalid offset!";

  size_t write_size = std::min(size, this->size() - pos);
  FXL_DCHECK(write_size <= out_buffer->size()) << "|out_buffer| is not large enough for copy!";

  std::memcpy(out_buffer->mutable_data(), data() + pos, write_size);
  return write_size;
}

const BufferView ByteBuffer::view(size_t pos, size_t size) const {
  FXL_DCHECK(pos <= this->size()) << "|pos| contains an invalid offset!";
  return BufferView(data() + pos, std::min(size, this->size() - pos));
}

fxl::StringView ByteBuffer::AsString() const {
  return fxl::StringView(reinterpret_cast<const char*>(data()), size());
}

std::string ByteBuffer::ToString() const {
  return AsString().ToString();
}

void MutableByteBuffer::Write(const uint8_t* data, size_t size, size_t pos) {
  if (!size) return;

  FXL_DCHECK(data);
  FXL_DCHECK(pos <= this->size()) << "|pos| contains an invalid offset!";
  FXL_DCHECK(size <= this->size() - pos)
      << "Buffer not large enough! (required: " << size << ", available: " << this->size() - pos;

  std::memcpy(mutable_data() + pos, data, size);
}

MutableBufferView MutableByteBuffer::mutable_view(size_t pos, size_t size) {
  FXL_DCHECK(pos <= this->size()) << "|pos| contains an invalid offset!";
  return MutableBufferView(mutable_data() + pos, std::min(size, this->size() - pos));
}

DynamicByteBuffer::DynamicByteBuffer() : buffer_size_(0u) {}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size) : buffer_size_(buffer_size) {
  if (buffer_size == 0) return;
  buffer_ = std::make_unique<uint8_t[]>(buffer_size);

  // TODO(armansito): For now this is dumb but we should properly handle the
  // case when we're out of memory.
  FXL_DCHECK(buffer_.get()) << "Failed to allocate buffer";
}

DynamicByteBuffer::DynamicByteBuffer(const ByteBuffer& buffer)
    : buffer_size_(buffer.size()),
      buffer_(buffer.size() ? std::make_unique<uint8_t[]>(buffer.size()) : nullptr) {
  FXL_DCHECK(!buffer_size_ || buffer_.get())
      << "|buffer| cannot be nullptr when |buffer_size| is non-zero";
  buffer.Copy(this);
}

DynamicByteBuffer::DynamicByteBuffer(size_t buffer_size, std::unique_ptr<uint8_t[]> buffer)
    : buffer_size_(buffer_size), buffer_(std::move(buffer)) {
  FXL_DCHECK(!buffer_size_ || buffer_.get())
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

void DynamicByteBuffer::Fill(uint8_t value) {
  memset(buffer_.get(), value, buffer_size_);
}

ByteBuffer::const_iterator DynamicByteBuffer::cbegin() const {
  return buffer_.get();
}

ByteBuffer::const_iterator DynamicByteBuffer::cend() const {
  return buffer_.get() + buffer_size_;
}

BufferView::BufferView(const ByteBuffer& buffer, size_t size) {
  *this = buffer.view(0u, size);
}

BufferView::BufferView(const fxl::StringView& string) {
  size_ = string.size();
  bytes_ = reinterpret_cast<const uint8_t*>(string.data());
}

BufferView::BufferView(const uint8_t* bytes, size_t size) : size_(size), bytes_(bytes) {
  // If |size| non-zero then |bytes| cannot be nullptr.
  FXL_DCHECK(!size_ || bytes_) << "|bytes_| cannot be nullptr if |size_| > 0";
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
  FXL_DCHECK(buffer);
  size_ = buffer->size();
  bytes_ = buffer->mutable_data();
}

MutableBufferView::MutableBufferView(uint8_t* bytes, size_t size) : size_(size), bytes_(bytes) {
  // If |size| non-zero then |bytes| cannot be nullptr.
  FXL_DCHECK(!size_ || bytes_) << "|bytes_| cannot be nullptr if |size_| > 0";
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

void MutableBufferView::Fill(uint8_t value) {
  memset(bytes_, value, size_);
}

}  // namespace common
}  // namespace bluetooth
