// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INPUT_REPORT_READER_RING_BUFFER_H_
#define LIB_INPUT_REPORT_READER_RING_BUFFER_H_

#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <type_traits>
#include <utility>

namespace input_report_reader {

// |RingBuffer| is a statically-allocated, typed ring buffer container.
//  This container is not thread safe.
// T: the type of elements.
// N: the maximum number of elements/capacity.
template <typename T, uint32_t N>
class __OWNER(T) RingBuffer final {
 public:
  constexpr RingBuffer() = default;
  ~RingBuffer() { clear(); }

  // Number of elements
  uint32_t size() const { return size_; }
  static constexpr uint32_t capacity() { return N; }
  bool empty() const { return size_ == 0; }
  bool full() const { return size_ == N; }

  // It is illegal to call front on an empty RingBuffer.
  T& front() {
    ZX_DEBUG_ASSERT(size_ != 0);
    return *GetIndex(head_);
  }

  // It is illegal to call back on an empty RingBuffer.
  T& back() {
    ZX_DEBUG_ASSERT(size_ != 0);
    return *GetIndex(Previous(tail_));
  }

  // It is illegal to call pop on an empty RingBuffer.
  void pop() {
    ZX_DEBUG_ASSERT(size_ != 0);

    GetIndex(head_)->~T();

    head_ = Next(head_);
    size_--;
  }

  // It is illegal to call push on a full RingBuffer.
  void push(T obj) {
    ZX_DEBUG_ASSERT(size_ < N);

    new (GetIndex(tail_)) T(std::move(obj));

    tail_ = Next(tail_);
    size_++;
  }

  // It is illegal to call emplace on a full RingBuffer.
  template <class... Args>
  void emplace(Args&&... args) {
    ZX_DEBUG_ASSERT(size_ < N);

    new (GetIndex(tail_)) T(std::forward<Args...>(args)...);

    tail_ = Next(tail_);
    size_++;
  }

  // Remove all elements in the RingBuffer.
  void clear() {
    // We only need to explicitly destruct elements if they aren't trivially destructible.
    if constexpr (!std::is_trivially_destructible_v<T>) {
      while (!empty()) {
        pop();
      }
    }
    head_ = tail_ = size_ = 0;
  }

 private:
  inline T* GetIndex(uint32_t index) { return reinterpret_cast<T*>(data_) + index; }
  static inline uint32_t Previous(uint32_t index) { return (index == 0) ? (N - 1) : (index - 1); }
  static inline uint32_t Next(uint32_t index) { return (index == (N - 1)) ? 0 : (index + 1); }
  alignas(alignof(T)) uint8_t data_[sizeof(T) * N];

  // |head_| is the index offset to the oldest element in the buffer.
  uint32_t head_ = 0;
  // |tail_| is the index offset to the empty slot where the next element should be inserted.
  uint32_t tail_ = 0;
  uint32_t size_ = 0;
};

}  // namespace input_report_reader

#endif  // LIB_INPUT_REPORT_READER_RING_BUFFER_H_
