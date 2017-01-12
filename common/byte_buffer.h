// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <cstdint>
#include <memory>

// TODO(armansito): Probably introduce BT_DCHECK, etc, so that we can avoid
// including all of lib/ftl/logging.h
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace bluetooth {
namespace common {

// Interface for buffer implementations with various allocation schemes.
class ByteBuffer {
 public:
  using const_iterator = const uint8_t*;
  using iterator = const_iterator;

  // Returns a pointer to the beginning of this buffer. May return nullptr if
  // the buffer has size 0.
  virtual const uint8_t* GetData() const = 0;
  virtual uint8_t* GetMutableData() = 0;

  // Returns the number of bytes contained in this packet.
  virtual size_t GetSize() const = 0;

  // Sets the contents of the buffer to 0s.
  virtual void SetToZeros() = 0;

  // Releases the contents of the buffer in a new dynamically allocated buffer.
  // Depending on the implementation this may allocate new memory and perform a
  // copy operation or it may simply move the memory contents. After a
  // successful move the contents of this ByteBuffer instance will be
  // invalidated and all subsequent calls to GetData() and GetMutableData() will
  // return a nullptr.
  virtual std::unique_ptr<uint8_t[]> MoveContents() = 0;

  // Iterator functions.
  iterator begin() const { return cbegin(); }
  iterator end() const { return cend(); }
  virtual const_iterator cbegin() const = 0;
  virtual const_iterator cend() const = 0;
};

// A ByteBuffer with static storage duration. Instances of this class are
// copyable. Due to the static buffer storage duration, move semantics work the
// same way as copy semantics, i.e. moving an instance will copy the buffer
// contents.
template <size_t BufferSize>
class StaticByteBuffer : public ByteBuffer {
 public:
  StaticByteBuffer() : buffer_size_(BufferSize) {
    static_assert(BufferSize, "|BufferSize| must be non-zero");
  }

  // ByteBuffer overrides
  const uint8_t* GetData() const override {
    return buffer_size_ ? buffer_.data() : nullptr;
  }

  uint8_t* GetMutableData() override {
    return buffer_size_ ? buffer_.data() : nullptr;
  }

  size_t GetSize() const override { return buffer_size_; }
  void SetToZeros() override { buffer_.fill(0); }

  // While a regular move operation between StaticByteBuffer instances performs
  // a copy and leaves the source buffer intact, MoveContents() invalidates the
  // source buffer to conform to the ByteBuffer interface.
  std::unique_ptr<uint8_t[]> MoveContents() override {
    if (!buffer_size_)
      return nullptr;

    auto new_buffer = std::make_unique<uint8_t[]>(buffer_size_);
    FTL_DCHECK(new_buffer.get());
    memcpy(new_buffer.get(), buffer_.data(), buffer_size_);
    buffer_size_ = 0u;
    return new_buffer;
  }

  const_iterator cbegin() const override {
    return buffer_size_ ? buffer_.cbegin() : buffer_.cend();
  }

  const_iterator cend() const override { return buffer_.cend(); }

 private:
  size_t buffer_size_;
  std::array<uint8_t, BufferSize> buffer_;
};

// A ByteBuffer with dynamic storage duration. The underlying buffer is
// allocated using malloc. Instances of this class are move-only.
//
// TODO(armansito): If our libc malloc implementation proves to be inefficient
// for data packets, we should allow for more efficient allocation schemes, e.g.
// by using the trait/mixin pattern.
class DynamicByteBuffer : public ByteBuffer {
 public:
  // Allocates a new buffer with |buffer_size| bytes.
  explicit DynamicByteBuffer(size_t buffer_size);

  // Takes ownership of |buffer| and avoids allocating a new buffer. Since this
  // constructor performs a simple assignment, the caller must make sure that
  // the buffer pointed to by |buffer| actually contains |buffer_size| bytes.
  DynamicByteBuffer(size_t buffer_size, std::unique_ptr<uint8_t[]> buffer);

  // Move constructor and assignment operator
  DynamicByteBuffer(DynamicByteBuffer&& other);
  DynamicByteBuffer& operator=(DynamicByteBuffer&& other);

  // ByteBuffer overrides
  const uint8_t* GetData() const override;
  uint8_t* GetMutableData() override;
  size_t GetSize() const override;
  void SetToZeros() override;
  std::unique_ptr<uint8_t[]> MoveContents() override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

 private:
  DynamicByteBuffer() = default;

  // Pointer to the underlying buffer, which is owned and managed by us.
  size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DynamicByteBuffer);
};

}  // namespace common
}  // namespace bluetooth
