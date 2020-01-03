// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_BLOCKING_RING_BUFFER_H_
#define STORAGE_BUFFER_BLOCKING_RING_BUFFER_H_

#include <memory>
#include <utility>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <storage/buffer/ring_buffer.h>

namespace storage {

class BlockingRingBufferReservation;

namespace internal {

class BlockingRingBufferImpl {
 public:
  explicit BlockingRingBufferImpl(std::unique_ptr<RingBuffer> buffer);
  BlockingRingBufferImpl(const BlockingRingBufferImpl&) = delete;
  BlockingRingBufferImpl& operator=(const BlockingRingBufferImpl&) = delete;
  BlockingRingBufferImpl(BlockingRingBufferImpl&& other) = delete;
  BlockingRingBufferImpl& operator=(BlockingRingBufferImpl&& other) = delete;
  ~BlockingRingBufferImpl() = default;

  zx_status_t Reserve(uint64_t blocks, BlockingRingBufferReservation* out);

  // Identifies that a RingBufferReservation is going out of scope, implying that there may be
  // additional space in |buffer_| now that the reservation has reset or destroyed. Signals to
  // blocked callers of |Reserve()| that this additional space is available.
  void Wake();

  size_t capacity() const { return buffer_->capacity(); }

 private:
  std::unique_ptr<RingBuffer> buffer_;
  fbl::Mutex lock_;
  // Protect this condition variable with the lock itself to avoid missing notifications.
  fbl::ConditionVariable cvar_ __TA_GUARDED(lock_);
};

}  // namespace internal

// A wrapper around |RingBuffer| which enables callers to block their calling
// thread while invoking |Reserve| if no space is available. Callers are automatically
// woken up when space is made available.
//
// This class is not movable or copyable.
// This class is thread-safe.
class BlockingRingBuffer {
 public:
  BlockingRingBuffer(const BlockingRingBuffer&) = delete;
  BlockingRingBuffer& operator=(const BlockingRingBuffer&) = delete;
  BlockingRingBuffer(BlockingRingBuffer&& other) = delete;
  BlockingRingBuffer& operator=(BlockingRingBuffer&& other) = delete;
  ~BlockingRingBuffer() = default;

  static zx_status_t Create(VmoidRegistry* vmoid_registry, size_t blocks, uint32_t block_size,
                            const char* label, std::unique_ptr<BlockingRingBuffer>* out);

  // Same as |RingBuffer.Reserve|, but only returns ZX_ERR_NO_SPACE if |blocks| is greater
  // than capacity. In all other cases, blocks the caller until space is available.
  zx_status_t Reserve(uint64_t blocks, BlockingRingBufferReservation* out) {
    return buffer_.Reserve(blocks, out);
  }

  size_t capacity() const { return buffer_.capacity(); }

 private:
  BlockingRingBuffer(std::unique_ptr<RingBuffer> buffer);

  internal::BlockingRingBufferImpl buffer_;
};

// A wrapper around |RingBufferReservation| which automatically notifies blocked callers of
// |BlockingRingBuffer.Reserve| when space is made available (on the destruction of an existing
// reservation).
//
// This class is movable, but not copyable.
// This class is thread-compatible.
class BlockingRingBufferReservation final : public RingBufferReservation {
 public:
  BlockingRingBufferReservation() = default;
  BlockingRingBufferReservation(internal::BlockingRingBufferImpl* buffer,
                                RingBufferReservation reservation)
      : RingBufferReservation(std::move(reservation)), buffer_(buffer) {}
  BlockingRingBufferReservation(const BlockingRingBufferReservation&) = delete;
  BlockingRingBufferReservation& operator=(const BlockingRingBufferReservation&) = delete;
  BlockingRingBufferReservation(BlockingRingBufferReservation&& other) = default;
  BlockingRingBufferReservation& operator=(BlockingRingBufferReservation&& other) = default;
  ~BlockingRingBufferReservation();

 private:
  internal::BlockingRingBufferImpl* buffer_ = nullptr;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_BLOCKING_RING_BUFFER_H_
