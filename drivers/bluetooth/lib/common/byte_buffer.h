// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace bluetooth {
namespace common {

class BufferView;
class MutableBufferView;
class MutableByteBuffer;

// Interface for buffer implementations with various allocation schemes.
class ByteBuffer {
 public:
  using const_iterator = const uint8_t*;
  using iterator = const_iterator;

  virtual ~ByteBuffer() = default;

  // Returns a pointer to the beginning of this buffer. The return value is undefined if
  // the buffer has size 0.
  virtual const uint8_t* data() const = 0;

  // Returns the number of bytes contained in this packet.
  virtual size_t size() const = 0;

  // Returns a BufferView that points to the region of this buffer starting at |pos|
  // of |size| bytes. If |size| is larger than the size of this BufferView then the returned region
  // will contain all bytes in this buffer starting at |pos|.
  //
  // For example:
  //
  //  // Get a view of all of |my_buffer|.
  //  const BufferView view = my_buffer.view();
  //
  //  // Get a view of the first 5 bytes in |my_buffer| (assuming |my_buffer| is large enough).
  //  view = my_buffer.view(0, 5);
  //
  //  // Get a view of |my_buffer| starting at the second byte.
  //  view = my_buffer.view(2);
  //
  const BufferView view(size_t pos = 0,
                        size_t size = std::numeric_limits<std::size_t>::max()) const;

  // Copies |size| bytes of this buffer into |out_buffer| starting at offset |pos| and returns the
  // number of bytes that were copied. |out_buffer| must be large enough to accomodate the result of
  // this operation.
  size_t Copy(MutableByteBuffer* out_buffer, size_t pos = 0,
              size_t size = std::numeric_limits<std::size_t>::max()) const;

  // Iterator functions.
  iterator begin() const { return cbegin(); }
  iterator end() const { return cend(); }
  virtual const_iterator cbegin() const = 0;
  virtual const_iterator cend() const = 0;

  // Read-only random access operator.
  inline const uint8_t& operator[](size_t pos) const {
    FXL_DCHECK(pos < size()) << "Invalid offset (pos = " << pos << ")!";
    return data()[pos];
  }

  // Returns the contents of this buffer as a C++ string-like object without copying its contents.
  fxl::StringView AsString() const;

  // Returns the contents of this buffer as a C++ string after copying its contents.
  std::string ToString() const;
};

// Mutable extension to the ByteBuffer interface. This provides methods that
// allows durect mutable access to the underlying buffer.
class MutableByteBuffer : public ByteBuffer {
 public:
  ~MutableByteBuffer() override = default;

  // Returns a pointer to the beginning of this buffer. The return value is undefined if
  // the buffer has size 0.
  virtual uint8_t* mutable_data() = 0;

  // Random access operator that allows mutations.
  inline uint8_t& operator[](size_t pos) {
    FXL_DCHECK(pos < size()) << "Invalid offset (pos = " << pos << ")!";
    return mutable_data()[pos];
  }

  // Writes the contents of |data| into this buffer starting at |pos|.
  inline void Write(const ByteBuffer& data, size_t pos = 0) {
    Write(data.data(), data.size(), pos);
  }

  // Writes |size| octets of data starting from |data| into this buffer starting at |pos|. |data|
  // must point to a valid piece of memory if |size| is non-zero. If |size| is zero, then this
  // operation is a NOP.
  void Write(const uint8_t* data, size_t size, size_t pos = 0);

  // Behaves exactly like ByteBuffer::View but returns the result in a MutableBufferView instead.
  MutableBufferView mutable_view(size_t pos = 0,
                                 size_t size = std::numeric_limits<std::size_t>::max());

  // Sets the contents of the buffer to 0s.
  void SetToZeros() { Fill(0); }

  // Fills the contents of the buffer with the given value.
  virtual void Fill(uint8_t value) = 0;
};

// A ByteBuffer with static storage duration. Instances of this class are
// copyable. Due to the static buffer storage duration, move semantics work the
// same way as copy semantics, i.e. moving an instance will copy the buffer
// contents.
template <size_t BufferSize>
class StaticByteBuffer : public MutableByteBuffer {
 public:
  StaticByteBuffer() { static_assert(BufferSize, "|BufferSize| must be non-zero"); }
  ~StaticByteBuffer() override = default;

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
  const uint8_t* data() const override { return buffer_.data(); }
  size_t size() const override { return buffer_.size(); }
  const_iterator cbegin() const override { return buffer_.cbegin(); }
  const_iterator cend() const override { return buffer_.cend(); }

  // MutableByteBuffer overrides:
  uint8_t* mutable_data() override { return buffer_.data(); }
  void Fill(uint8_t value) override { buffer_.fill(value); }

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
  ~DynamicByteBuffer() override = default;

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
  const uint8_t* data() const override;
  size_t size() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

  // MutableByteBuffer overrides:
  uint8_t* mutable_data() override;
  void Fill(uint8_t value) override;

 private:
  // Pointer to the underlying buffer, which is owned and managed by us.
  size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DynamicByteBuffer);
};

// A ByteBuffer that does not own the memory that it points to but rather
// provides an immutable view over it.
class BufferView final : public ByteBuffer {
 public:
  BufferView(const uint8_t* bytes, size_t size);
  ~BufferView() override = default;

  explicit BufferView(const ByteBuffer& buffer,
                      size_t size = std::numeric_limits<std::size_t>::max());
  explicit BufferView(const fxl::StringView& string);

  // The default constructor initializes this to an empty buffer.
  BufferView();

  // ByteBuffer overrides:
  const uint8_t* data() const override;
  size_t size() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

 private:
  size_t size_;
  const uint8_t* bytes_;
};

// A ByteBuffer that does not own the memory that it points to but rather
// provides a mutable view over it.
class MutableBufferView final : public MutableByteBuffer {
 public:
  explicit MutableBufferView(MutableByteBuffer* buffer);
  MutableBufferView(uint8_t* bytes, size_t size);
  ~MutableBufferView() override = default;

  // The default constructor initializes this to an empty buffer.
  MutableBufferView();

  // ByteBuffer overrides:
  const uint8_t* data() const override;
  size_t size() const override;
  const_iterator cbegin() const override;
  const_iterator cend() const override;

  // MutableByteBuffer overrides:
  uint8_t* mutable_data() override;
  void Fill(uint8_t value) override;

 private:
  size_t size_;
  uint8_t* bytes_;
};

}  // namespace common
}  // namespace bluetooth
