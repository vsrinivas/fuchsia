// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_RING_BUFFER_H_
#define STORAGE_BUFFER_RING_BUFFER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/status.h>

#include <vector>

#include <fbl/mutex.h>
#include <fbl/span.h>
#include <fbl/vector.h>
#include <storage/buffer/block_buffer_view.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/operation/unbuffered_operation.h>

namespace storage {

class RingBufferReservation;

namespace internal {

// Internal state backing RingBuffer. Refer to that class for the public API.
//
// This class is not movable or copyable.
// This class is thread-safe.
class RingBufferState {
 public:
  explicit RingBufferState(VmoBuffer buffer)
      : buffer_(std::move(buffer)), reserved_start_(0), reserved_length_(0) {}
  RingBufferState(const RingBufferState&) = delete;
  RingBufferState& operator=(const RingBufferState&) = delete;
  RingBufferState(RingBufferState&& other) = delete;
  RingBufferState& operator=(RingBufferState&& other) = delete;
  ~RingBufferState() = default;

  // Reserves space for |blocks| contiguous blocks in the circular buffer.
  //
  // To perform optimally, these reservations should be destroyed in the same order
  // they are reserved.
  //
  // |blocks| must be greater than zero.
  // Returns ZX_ERR_NO_SPACE if there is not enough room.
  zx_status_t Reserve(uint64_t blocks, RingBufferReservation* out);

  // Returns the total amount of pending blocks which may be buffered.
  size_t capacity() const { return buffer_.capacity(); }

  uint32_t BlockSize() const { return buffer_.BlockSize(); }

  // Frees |reservation| from the buffer.
  //
  // Only callable by |RingBufferReservation|, since this frees the previously used
  // reservation.
  void Free(const RingBufferReservation& reservation);

  // Returns data starting at block |index| in the buffer.
  //
  // Only callable by |RingBufferReservation|, since this uses the previously created
  // reservation.
  void* Data(size_t index) { return buffer_.Data(index); }

  // Returns a pointer to the underlying buffer. Should only be accessible to the
  // |RingBufferReservation|, which should take caution to only reference reserved portions
  // of the buffer itself.
  VmoBuffer* buffer() { return &buffer_; }
  const VmoBuffer* buffer() const { return &buffer_; }

  // Returns the vmoid of the underlying RingBuffer.
  //
  // Only callable by |RingBufferReservation|, since this uses the previously created
  // reservation.
  vmoid_t vmoid() const { return buffer_.vmoid(); }

 private:
  struct Range {
    size_t start;
    size_t length;
  };

  // Returns true if there is space available for |blocks| blocks within the buffer.
  bool IsSpaceAvailableLocked(size_t blocks) const __TA_REQUIRES(lock_);

  // Actually frees |blocks| in the buffer at |start| in the buffer.
  void CompleteFreeLocked(size_t start, size_t blocks) __TA_REQUIRES(lock_);

  VmoBuffer buffer_;

  // Although this lock guards some fields of |RingBuffer| explicitly, access to the
  // buffer data ("who can access the region at [start, start + length)?") is implicit
  // via the RingBufferReservation objects.
  fbl::Mutex lock_;

  // The units of all the following are "filesystem blocks".
  size_t reserved_start_ __TA_GUARDED(lock_) = 0;
  size_t reserved_length_ __TA_GUARDED(lock_) = 0;
  // TODO(ZX-4033): Replace fbl::Vector with a friendlier std container when possible.
  fbl::Vector<Range> pending_free_ __TA_GUARDED(lock_);
};

}  // namespace internal

// A reservation of space within |RingBuffer|. Allows clients to safely access a portion
// of the circular buffer for either reading or writing.
//
// Releases the space when going out of scope (or reset).
//
// This class is movable, but not copyable.
// This class is thread-compatible.
class RingBufferReservation {
 public:
  RingBufferReservation() = default;

  // Creates a RingBufferReservation within a buffer, at |start| blocks within
  // the buffer, of |length| bytes long. [start, start + length) may wrap around the
  // RingBuffer.
  RingBufferReservation(internal::RingBufferState* buffer, size_t start, size_t length);
  RingBufferReservation(const RingBufferReservation&) = delete;
  RingBufferReservation& operator=(const RingBufferReservation&) = delete;
  RingBufferReservation(RingBufferReservation&& other);
  RingBufferReservation& operator=(RingBufferReservation&& other);
  virtual ~RingBufferReservation();

  // Copies from |in_requests|, at the provided |offset| into this reservation.
  //
  // Updates the in-memory offsets of |requests| so they point to the correct offsets in the
  // in-memory buffer instead of their original VMOs, outputting these updated requests.
  //
  // Returns an error if a VMO from |requests| cannot be accessed to write into
  // the buffer, but otherwise returns the number of blocks copied.
  //
  // Preconditions:
  // - The reservation must be large enough to copy |requests|:
  //  - offset + BlockCount(in_requests) <= length()
  // - |Reserved()| must be true.
  zx::status<uint64_t> CopyRequests(fbl::Span<const storage::UnbufferedOperation> in_operations,
                                    size_t offset,
                                    std::vector<storage::BufferedOperation>* out_operations);

  BlockBufferView buffer_view() { return view_; }

  // The first reservation block, relative to the start of |RingBuffer|.
  size_t start() const { return view_.start(); }

  // The total length of this reservation, in blocks.
  size_t length() const { return view_.length(); }

  vmoid_t vmoid() const;

  // Returns one block of data starting at block |index| within this reservation.
  // Since this data has been reserved, |RingBuffer| will not attempt to access it concurrently.
  //
  // Preconditions:
  // - |Reserved()| must be true.
  // - |index| < |length()|
  void* Data(size_t index);
  const void* Data(size_t index) const;

 protected:
  // Returns true if the reservation holds blocks in a |RingBuffer|.
  bool Reserved() const { return buffer_ != nullptr; }

  // Unreserves the reservation. This will cause |Reserved()| to return
  // false for the duration of the |RingBufferReservation|'s lifetime.
  void Reset();

 private:
  internal::RingBufferState* buffer_ = nullptr;
  BlockBufferView view_;
};

// In-memory circular buffer.
//
// This class is not movable or copyable.
// This class is thread-safe.
class RingBuffer {
 public:
  explicit RingBuffer(VmoBuffer buffer) : state_(std::move(buffer)) {}
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;
  RingBuffer(RingBuffer&& other) = delete;
  RingBuffer& operator=(RingBuffer&& other) = delete;
  ~RingBuffer() = default;

  // Initializes the buffer with |blocks| blocks of size |block_size|.
  static zx_status_t Create(VmoidRegistry* vmoid_registry, size_t blocks, uint32_t block_size,
                            const char* label, std::unique_ptr<RingBuffer>* out);

  // Reserves space for |blocks| contiguous blocks in the circular buffer.
  //
  // To perform optimally, these reservations should be destroyed in the same order
  // they are reserved.
  //
  // |blocks| must be greater than zero.
  // Returns ZX_ERR_NO_SPACE if there is not enough room.
  zx_status_t Reserve(uint64_t blocks, RingBufferReservation* out) {
    return state_.Reserve(blocks, out);
  }

  // Returns the total amount of pending blocks which may be buffered.
  size_t capacity() const { return state_.capacity(); }
  uint32_t BlockSize() const { return state_.buffer()->BlockSize(); }

 private:
  internal::RingBufferState state_;
};

// A utility class, holding a collection of write requests associated with a portion of a single
// RingBuffer, ready to be transmitted to persistent storage.
//
// This class is movable, but not copyable.
// This class is thread-safe.
class RingBufferRequests {
 public:
  RingBufferRequests() = default;
  RingBufferRequests(std::vector<storage::BufferedOperation> requests,
                     RingBufferReservation reservation);
  RingBufferRequests(const RingBufferRequests&) = delete;
  RingBufferRequests& operator=(const RingBufferRequests&) = delete;
  RingBufferRequests(RingBufferRequests&& other) = default;
  RingBufferRequests& operator=(RingBufferRequests&& other) = default;
  ~RingBufferRequests() = default;

  const std::vector<storage::BufferedOperation>& Operations() const { return requests_; }
  RingBufferReservation* Reservation() { return &reservation_; }

 private:
  std::vector<storage::BufferedOperation> requests_;
  RingBufferReservation reservation_;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_RING_BUFFER_H_
