// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_BUFFER_VIEW_H_
#define SRC_STORAGE_MINFS_BUFFER_VIEW_H_

#include <lib/fit/function.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <variant>

#include <storage/buffer/block_buffer.h>

#include "block_utils.h"

namespace minfs {

// Wraps either a regular pointer or a BlockBuffer. This exists because the mapped address for a
// storage::BlockBuffer isn't stable. In particular, a BlockBuffer that happens to be a resizeable
// VMO, can have its mapping change when it grows. When that happens, we don't want a BufferView to
// be invalidated, so we wrap a BlockBuffer and always call through to get the current mapped
// address.
class BufferPtr {
 public:
  BufferPtr() : ptr_(std::in_place_index<0>, nullptr) {}

  BufferPtr(const BufferPtr&) = default;
  BufferPtr& operator=(const BufferPtr&) = default;

  static BufferPtr FromMemory(void* buffer) {
    return BufferPtr(Ptr(std::in_place_index<0>, buffer));
  }

  static BufferPtr FromBlockBuffer(storage::BlockBuffer* buffer) {
    return BufferPtr(Ptr(std::in_place_index<1>, buffer));
  }

  void* get() const {
    if (std::holds_alternative<void*>(ptr_)) {
      return std::get<void*>(ptr_);
    } else {
      return std::get<storage::BlockBuffer*>(ptr_)->Data(0);
    }
  }

 private:
  using Ptr = std::variant<void*, storage::BlockBuffer*>;

  explicit BufferPtr(Ptr ptr) : ptr_(ptr) {}

  std::variant<void*, storage::BlockBuffer*> ptr_;
};

// BaseBufferView and BufferView are views of a buffer, a contiguous range in memory. It can be
// mutable or immutable. It keeps track of the use of mutable methods to record whether or not it is
// dirty. A flusher object is provided for flushing the buffer and is called via the Flush method if
// the buffer is deemed dirty. If no flusher is provided, the view is considered immutable. The
// underlying buffer can be memory, or it can be a BlockBuffer which we specialise for, in case
// BlockBuffer is resized, in which case its mapped address can change.
class BaseBufferView {
 public:
  using Flusher = fit::function<zx_status_t(BaseBufferView* view)>;

  BaseBufferView() = default;

  explicit BaseBufferView(BufferPtr buffer, size_t offset, size_t length)
      : buffer_(buffer), offset_(offset), length_(length) {}
  explicit BaseBufferView(BufferPtr buffer, size_t offset, size_t length, Flusher flusher)
      : buffer_(buffer), offset_(offset), length_(length), flusher_(std::move(flusher)) {}

  // Movable, but not copyable.
  BaseBufferView(BaseBufferView&& other) { *this = std::move(other); }
  BaseBufferView& operator=(BaseBufferView&& other);

  ~BaseBufferView();

  bool IsValid() const { return data() != nullptr; }
  void* data() const { return static_cast<uint8_t*>(buffer_.get()) + offset_; }
  size_t length() const { return length_; }
  size_t offset() const { return offset_; }
  ByteRange GetByteRange() const { return ByteRange(offset_, offset_ + length_); }
  bool dirty() const { return dirty_; }
  void set_dirty(bool v) {
    ZX_ASSERT(data() != nullptr);
    ZX_ASSERT(flusher_);
    dirty_ = v;
  }

  // Does nothing if the buffer is not dirty. The buffer is always marked clean after calling flush;
  // it is up to the caller to handle errors appropriately.
  [[nodiscard]] zx_status_t Flush();

 protected:
  // N.B. Take care with the 'as' methods and alignment. On some architectures, unaligned access is
  // a problem, so if you're trying to access, say, a uint32_t at offset 5, you'll have an issue.

  // Returns const T&.
  template <typename T>
  const T& as() const {
    ZX_ASSERT(data() != nullptr);
    ZX_ASSERT(sizeof(T) <= length_);
    return *reinterpret_cast<T*>(data());
  }

  // Returns T&.
  template <typename T>
  T& as_mut() {
    ZX_ASSERT(data() != nullptr);
    ZX_ASSERT(sizeof(T) <= length_);
    ZX_ASSERT(flusher_);
    dirty_ = true;
    return *reinterpret_cast<T*>(data());
  }

 private:
  BufferPtr buffer_;
  size_t offset_ = 0;
  size_t length_ = 0;
  bool dirty_ = false;
  Flusher flusher_;
};

// BufferView is a typed version of BaseBufferView which will make it appear to be an array of
// objects of type T.
template <typename T>
class BufferView : public BaseBufferView {
 public:
  BufferView() = default;

  // |buffer| needs to be aligned sufficiently for T.
  BufferView(BufferPtr buffer, size_t index, size_t count)
      : BaseBufferView(buffer, sizeof(T) * index, sizeof(T) * count) {}
  BufferView(BufferPtr buffer, size_t index, size_t count, Flusher flusher)
      : BaseBufferView(buffer, sizeof(T) * index, sizeof(T) * count, std::move(flusher)) {}

  // Movable, but not copyable.
  BufferView(BufferView&& other) = default;
  BufferView& operator=(BufferView&& other) = default;

  size_t count() const { return length() / sizeof(T); }

  // Non mutating accessors.
  const T& operator*() const { return as<T>(); }
  const T& operator[](size_t index) const {
    ZX_ASSERT(index < count());
    return (&as<T>())[index];
  }

  // Mutating accessors.
  T& mut_ref() { return as_mut<T>(); }
  T& mut_ref(size_t index) {
    ZX_ASSERT(index < count());
    return (&as_mut<T>())[index];
  }
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_BUFFER_VIEW_H_
