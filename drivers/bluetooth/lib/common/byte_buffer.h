// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

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

  // Returns the number of bytes contained in this packet.
  virtual size_t GetSize() const = 0;

  // Copy the contents of the underlying buffer and return them in a new buffer.
  virtual std::unique_ptr<uint8_t[]> CopyContents() const;

  // Iterator functions.
  iterator begin() const { return cbegin(); }
  iterator end() const { return cend(); }
  virtual const_iterator cbegin() const = 0;
  virtual const_iterator cend() const = 0;

  // Returns the contents of this buffer as a C++ string-like object without copying its contents.
  ftl::StringView AsString() const;

  // Returns the contents of this buffer as a C++ string after copying its contents.
  std::string ToString() const;
};

// Mutable extension to the ByteBuffer interface. This provides methods that
// allows durect mutable access to the underlying buffer.
class MutableByteBuffer : public ByteBuffer {
 public:
  // Returns a pointer to the beginning of this buffer. May return nullptr if
  // the buffer has size 0.
  virtual uint8_t* GetMutableData() = 0;

  // Sets the contents of the buffer to 0s.
  virtual void SetToZeros() = 0;
};

// A ByteBuffer with static storage duration. Instances of this class are
// copyable. Due to the static buffer storage duration, move semantics work the
// same way as copy semantics, i.e. moving an instance will copy the buffer
// contents.
template <size_t BufferSize>
class StaticByteBuffer : public MutableByteBuffer {
 public:
  StaticByteBuffer() { static_assert(BufferSize, "|BufferSize| must be non-zero"); }

  // Variadic template constructor to initialize a StaticByteBuffer using an
  // initializer_list e.g.:
  //
  //   StaticByteBuffer<3> foo{0x00, 0x01, 0x02};
  //   StaticByteBuffer<3> bar({0x00, 0x01, 0x02});
  //
  template <typename... T>
  StaticByteBuffer(T... bytes) : buffer_{{static_cast<uint8_t>(bytes)...}} {
    static_assert(BufferSize, "|BufferSize| must be non-zero");
    static_assert(BufferSize == sizeof...(T), "|BufferSize| must match initializer list count");
  }

  // ByteBuffer overrides
  const uint8_t* GetData() const override { return buffer_.data(); }
  size_t GetSize() const override { return buffer_.size(); }
  const_iterator cbegin() const override { return buffer_.cbegin(); }
  const_iterator cend() const override { return buffer_.cend(); }

  // MutableByteBuffer overrides:
  uint8_t* GetMutableData() override { return buffer_.data(); }
  void SetToZeros() override { buffer_.fill(0); }

 private:
  std::array<uint8_t, BufferSize> buffer_;
};

// Wrapper for the variadic template StaticByteBuffer constructor that deduces
// the value of the |BufferSize| template parameter from the given input. This
// way one can construct a StaticByteBuffer without hard-coding the size of the
// buffer like so:
//
//   auto buffer = common::CreateStaticByteBuffer(0x01, 0x02, 0x03);
//
template <typename... T>
StaticByteBuffer<sizeof...(T)> CreateStaticByteBuffer(T... bytes) {
  return StaticByteBuffer<sizeof...(T)>{bytes...};
}

// A ByteBuffer with dynamic storage duration. The underlying buffer is
// allocated using malloc. Instances of this class are move-only.
class DynamicByteBuffer : public MutableByteBuffer {
 public:
  // The default constructor creates an empty buffer with size 0.
  DynamicByteBuffer();

  // Allocates a new buffer with |buffer_size| bytes.
  explicit DynamicByteBuffer(size_t buffer_size);

  // Copies the contents of |buffer|.
  explicit DynamicByteBuffer(const ByteBuffer& buffer);

  // Takes ownership of |buffer| and avoids allocating a new buffer. Since this
  // constructor performs a simple assignment, the caller must make sure that
  // the buffer pointed to by |buffer| actually contains |buffer_size| bytes.
  DynamicByteBuffer(size_t buffer_size, std::unique_ptr<uint8_t[]> buffer);

  // Move constructor and assignment operator
  DynamicByteBuffer(DynamicByteBuffer&& other);
  DynamicByteBuffer& operator=(DynamicByteBuffer&& other);

  // ByteBuffer overrides:
  const uint8_t* GetData() const override;
  size_t GetSize() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

  // MutableByteBuffer overrides:
  uint8_t* GetMutableData() override;
  void SetToZeros() override;

 private:
  // Pointer to the underlying buffer, which is owned and managed by us.
  size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DynamicByteBuffer);
};

// A ByteBuffer that does not own the memory that it points to but rather
// provides an immutable view over it.
class BufferView : public ByteBuffer {
 public:
  BufferView(const uint8_t* bytes, size_t size);

  explicit BufferView(const ByteBuffer& buffer);
  explicit BufferView(const ftl::StringView& string);

  // The default constructor initializes this to an empty buffer.
  BufferView();

  // ByteBuffer overrides:
  const uint8_t* GetData() const override;
  size_t GetSize() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

 private:
  size_t size_;
  const uint8_t* bytes_;
};

// A ByteBuffer that does not own the memory that it points to but rather
// provides an mutable view over it.
class MutableBufferView : public MutableByteBuffer {
 public:
  explicit MutableBufferView(MutableByteBuffer* buffer);
  MutableBufferView(uint8_t* bytes, size_t size);

  // The default constructor initializes this to an empty buffer.
  MutableBufferView();

  // ByteBuffer overrides:
  const uint8_t* GetData() const override;
  size_t GetSize() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

  // MutableByteBuffer overrides:
  uint8_t* GetMutableData() override;
  void SetToZeros() override;

 private:
  size_t size_;
  uint8_t* bytes_;
};

}  // namespace common
}  // namespace bluetooth
