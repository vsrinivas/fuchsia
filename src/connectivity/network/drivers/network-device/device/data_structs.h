// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DATA_STRUCTS_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DATA_STRUCTS_H_

#include <zircon/assert.h>
#include <zircon/status.h>

#include <fbl/alloc_checker.h>

namespace network::internal {

// A ring buffer implementation that can store trivially-constructable types, whose capacity is
// informed on creation.
// This container is not thread-safe.
template <typename T>
class RingQueue {
 public:
  ~RingQueue() = default;

  // Creates a new queue with the given capacity and stores it in `out`.
  // Returns an error if the provided capacity is invalid, or it failed to allocate the required
  // memory.
  static zx_status_t Create(uint32_t capacity, std::unique_ptr<RingQueue<T>>* out) {
    if (capacity == 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    fbl::AllocChecker ac;
    std::unique_ptr<T[]> data(new (&ac) T[capacity]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    std::unique_ptr<RingQueue<T>> queue(new (&ac) RingQueue(std::move(data), capacity));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    *out = std::move(queue);
    return ZX_OK;
  }

  // Pushes a new value onto the queue.
  // It is invalid to push into a full queue.
  void Push(T value) {
    ZX_ASSERT(len_ < capacity_);
    data_[write_] = std::move(value);
    write_ = (write_ + 1) % capacity_;
    len_++;
  }

  // Pops a value from the queue.
  // It is invalid to pop from an empty queue.
  T Pop() {
    ZX_ASSERT(len_ > 0);
    T ret = std::move(data_[read_]);
    read_ = (read_ + 1) % capacity_;
    len_--;
    return ret;
  }

  // Peeks the next value from the queue, without popping.
  // It is invalid to peek into an empty queue.
  const T& Peek() const {
    ZX_ASSERT(len_ > 0);
    return data_[read_];
  }

  // Returns the number of elements currently queued.
  uint32_t count() const { return len_; }

 private:
  RingQueue(std::unique_ptr<T[]> data, uint32_t capacity)
      : data_(std::move(data)), capacity_(capacity), read_(0), write_(0), len_(0) {}
  std::unique_ptr<T[]> data_;
  uint32_t capacity_;
  uint32_t read_;
  uint32_t write_;
  uint32_t len_;
};

// A fixed capacity slab structure whose items are referenced by an index key.
// The slab's capacity is set on creation. This slab is only capable of storing trivially
// constructable and trivially destructible types.
// This container is not thread-safe.
template <typename T>
class IndexedSlab {
 public:
  // Creates a new slab with the given capacity and stores it in `out`.
  // Returns an error if the provided capacity is invalid, or it failed to allocate the required
  // memory.
  static zx_status_t Create(uint32_t capacity, std::unique_ptr<IndexedSlab<T>>* out) {
    if (capacity == 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    fbl::AllocChecker ac;
    std::unique_ptr<Holder[]> data(new (&ac) Holder[capacity]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    std::unique_ptr<IndexedSlab<T>> slab(new (&ac) IndexedSlab(std::move(data), capacity));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    *out = std::move(slab);
    return ZX_OK;
  }

  // Pushes a new entry into the slab, returning a key to retrieve it.
  // Pushing into a full slab is invalid.
  uint32_t Push(T data) {
    auto index = free_head_;
    ZX_ASSERT(index < capacity_);
    ZX_ASSERT(!data_[index].used);
    free_head_ = data_[index].slot.next;
    data_[index].slot.data = std::move(data);
    data_[index].used = true;
    used_++;
    return index;
  }

  // Gets the entry referenced by `index`.
  // Getting an invalid reference is invalid.
  T& Get(uint32_t index) {
    ZX_ASSERT(index < capacity_);
    ZX_ASSERT(data_[index].used);
    return data_[index].slot.data;
  }

  // Frees the entry referenced by `index`, returning the slot back to the Slab.
  // It is invalid to free an index which is not currently occupied.
  void Free(uint32_t index) {
    ZX_ASSERT(index < capacity_);
    ZX_ASSERT(data_[index].used);
    data_[index].used = false;
    data_[index].slot.next = free_head_;
    free_head_ = index;
    used_--;
  }

  // Gets the number of available slots in the slab.
  uint32_t available() const { return capacity_ - used_; }
  // Gets the number of occupied slots in the slab.
  uint32_t count() const { return used_; }
  // Gets the slab's capacity.
  uint32_t capacity() const { return capacity_; }

  class Iterator {
   public:
    explicit Iterator(const IndexedSlab<T>* parent, uint32_t idx = 0) : parent_(parent), cur_(idx) {
      if (cur_ < parent_->capacity_ && !parent_->data_[cur_].used) {
        ++(*this);
      }
    }

    bool operator==(const Iterator& rhs) const { return cur_ == rhs.cur_; }

    bool operator!=(const Iterator& rhs) const { return !(rhs == *this); }

    uint32_t operator*() const { return cur_; }

    Iterator& operator++() noexcept {
      if (cur_ < parent_->capacity_) {
        cur_++;
        while (cur_ < parent_->capacity_ && !parent_->data_[cur_].used) {
          cur_++;
        }
      }

      return *this;
    }

   private:
    // Pointer to indexed slab that created iterator, not owned.
    const IndexedSlab<T>* parent_;
    uint32_t cur_;
  };

  Iterator begin() const { return Iterator(this); }

  Iterator end() const { return Iterator(this, capacity_); }

 private:
  struct Holder {
    bool used;
    union {
      T data;
      uint32_t next;
    } slot;
  };

  IndexedSlab(std::unique_ptr<Holder[]> data, uint32_t capacity)
      : data_(std::move(data)), capacity_(capacity), free_head_(0), used_(0) {
    for (uint32_t i = 0; i < capacity_; i++) {
      data_[i].used = false;
      data_[i].slot.next = i + 1;
    }
  }

  std::unique_ptr<Holder[]> data_;
  uint32_t capacity_;
  uint32_t free_head_;
  uint32_t used_;
};

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DATA_STRUCTS_H_
