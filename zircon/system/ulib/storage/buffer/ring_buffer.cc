// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/ring_buffer.h"

#include <zircon/status.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <fbl/auto_lock.h>
#include <fs/trace.h>

namespace storage {

zx_status_t internal::RingBufferState::Reserve(uint64_t blocks, RingBufferReservation* out) {
  if (blocks == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t destination_offset = 0;
  {
    fbl::AutoLock lock(&lock_);
    if (!IsSpaceAvailableLocked(blocks)) {
      return ZX_ERR_NO_SPACE;
    }
    destination_offset = (reserved_start_ + reserved_length_) % capacity();
    reserved_length_ += blocks;
  }
  *out = RingBufferReservation(this, destination_offset, blocks);
  return ZX_OK;
}

void internal::RingBufferState::Free(const RingBufferReservation& reservation) {
  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT_MSG(reservation.length() <= reserved_length_,
                      "Attempting to free more blocks than available");

  // To perform optimally, the RingBuffer is expected to operate as a FIFO allocator,
  // where requests are freed in the same order they are allocated.
  // Under these conditions, reservations are always freed from the back
  // of the reserved portion of the buffer, and freeing is a simple update
  // of "start and length". However, if the reservations are not freed in this order,
  // they are put into a "pending list", where freeing the reservation is delayed
  // until we *can* release a reservation in-order.
  //
  // Since the common case of execution is in-order releasing, we use a simple vector for
  // tracking allocatable space. This performs poorly for a high volume of out-of-order
  // frees, but performs reasonably well when out-of-order operation freeing is relatively rare.

  if (reserved_start_ != reservation.start()) {
    // Freeing reservation out-of-order.
    //
    // Ensure "pending_free_" stays sorted by the start index.
    for (size_t i = 0; i < pending_free_.size(); i++) {
      if (reservation.start() < pending_free_[i].start) {
        pending_free_.insert(i, Range{reservation.start(), reservation.length()});
        return;
      }
    }
    pending_free_.push_back(Range{reservation.start(), reservation.length()});
    return;
  }

  CompleteFreeLocked(reservation.start(), reservation.length());

  while (!pending_free_.is_empty()) {
    // We have already ensured "pending_free_" is sorted by start index.
    //
    // This means that we can try releasing previously freed operations
    // in-order.
    if (pending_free_[0].start != reserved_start_) {
      return;
    }
    CompleteFreeLocked(pending_free_[0].start, pending_free_[0].length);
    pending_free_.erase(0);
  }
}

bool internal::RingBufferState::IsSpaceAvailableLocked(size_t blocks) const {
  if (blocks > capacity()) {
    FS_TRACE_WARN("storage: Requested reservation too large (%zu > %zu blocks)\n", blocks,
                  capacity());
  }
  return reserved_length_ + blocks <= capacity();
}

void internal::RingBufferState::CompleteFreeLocked(size_t start, size_t blocks) {
  ZX_DEBUG_ASSERT_MSG(start == reserved_start_, "Freeing out-of-order");

  size_t decommit_blocks = std::min(blocks, capacity() - reserved_start_);
  ZX_ASSERT(buffer_.vmo().op_range(ZX_VMO_OP_DECOMMIT, reserved_start_ * buffer_.BlockSize(),
                                   decommit_blocks * buffer_.BlockSize(), nullptr, 0) == ZX_OK);

  if (decommit_blocks < blocks) {
    // Wrapping around the circular buffer.
    decommit_blocks = blocks - decommit_blocks;
    ZX_ASSERT(buffer_.vmo().op_range(ZX_VMO_OP_DECOMMIT, 0, decommit_blocks * buffer_.BlockSize(),
                                     nullptr, 0) == ZX_OK);
  }

  reserved_start_ = (reserved_start_ + blocks) % capacity();
  reserved_length_ -= blocks;
}

RingBufferReservation::RingBufferReservation(internal::RingBufferState* buffer, size_t start,
                                             size_t length)
    : buffer_(buffer), view_(buffer->buffer(), start, length) {}

RingBufferReservation::RingBufferReservation(RingBufferReservation&& other)
    : buffer_(other.buffer_), view_(std::move(other.view_)) {
  other.buffer_ = nullptr;
  other.Reset();
  ZX_DEBUG_ASSERT(!other.Reserved());
}

RingBufferReservation& RingBufferReservation::operator=(RingBufferReservation&& other) {
  if (&other == this) {
    return *this;
  }
  Reset();
  buffer_ = other.buffer_;
  view_ = std::move(other.view_);
  other.buffer_ = nullptr;
  other.Reset();
  ZX_DEBUG_ASSERT(!other.Reserved());
  return *this;
}

RingBufferReservation::~RingBufferReservation() { Reset(); }

void RingBufferReservation::Reset() {
  if (Reserved()) {
    buffer_->Free(*this);
  }
  buffer_ = nullptr;
  view_ = BlockBufferView();
  ZX_DEBUG_ASSERT(!Reserved());
}

zx::status<size_t> RingBufferReservation::CopyRequests(
    fbl::Span<const storage::UnbufferedOperation> in_operations, size_t offset,
    std::vector<storage::BufferedOperation>* out_operations) {
  ZX_DEBUG_ASSERT_MSG(Reserved(), "Copying to invalid reservation");
  out_operations->reserve(out_operations->capacity() + in_operations.size());

  ZX_DEBUG_ASSERT_MSG(offset + BlockCount(in_operations) <= length(),
                      "Copying requests into a buffer beyond limit of prior reservation");

  const size_t capacity = buffer_->capacity();
  // Offset into this reservation.
  size_t reservation_offset = offset;
  // Offset into the target ring buffer.
  size_t ring_buffer_offset = (start() + reservation_offset) % capacity;
  size_t done = 0;

  for (size_t i = 0; i < in_operations.size(); i++) {
    // Read parameters of the current request.
    ZX_DEBUG_ASSERT_MSG(in_operations[i].op.type == storage::OperationType::kWrite,
                        "RingBuffer only accepts write requests");
    size_t vmo_offset = in_operations[i].op.vmo_offset;
    size_t dev_offset = in_operations[i].op.dev_offset;
    const size_t vmo_len = in_operations[i].op.length;
    ZX_DEBUG_ASSERT_MSG(vmo_len > 0, "Attempting to buffer empty request");

    // Calculate the offset/length we will need to write into the buffer.
    // Avoid writing beyond the end of the RingBuffer.
    size_t buf_len = std::min(vmo_len, capacity - ring_buffer_offset);

    // Verify that the length is valid.
    ZX_DEBUG_ASSERT_MSG(buf_len > 0, "Attempting to write zero-length request into buffer");
    const zx::unowned_vmo& vmo = in_operations[i].vmo;

    // Write data from the vmo into the buffer.
    void* ptr = Data(reservation_offset);

    zx_status_t status =
        vmo->read(ptr, vmo_offset * buffer_->BlockSize(), buf_len * buffer_->BlockSize());
    if (status != ZX_OK) {
      FS_TRACE_ERROR("fs: Failed to read from source buffer (%zu @ %zu): %s\n", buf_len, vmo_offset,
                     zx_status_get_string(status));
      return zx::error(status);
    }

    storage::BufferedOperation out_op;
    out_op.vmoid = vmoid();
    out_op.op.type = in_operations[i].op.type;
    out_op.op.vmo_offset = ring_buffer_offset;
    out_op.op.dev_offset = dev_offset;
    out_op.op.length = buf_len;
    out_operations->push_back(std::move(out_op));

    ring_buffer_offset = (ring_buffer_offset + buf_len) % capacity;
    reservation_offset += buf_len;

    if (buf_len != vmo_len) {
      // We wrapped around; write what remains from this request.
      vmo_offset += buf_len;
      dev_offset += buf_len;
      buf_len = vmo_len - buf_len;
      ZX_DEBUG_ASSERT(buf_len > 0);

      ptr = Data(reservation_offset);
      status = vmo->read(ptr, vmo_offset * buffer_->BlockSize(), buf_len * buffer_->BlockSize());
      if (status != ZX_OK) {
        FS_TRACE_ERROR("fs: Failed to read from source buffer (%zu @ %zu): %s\n", buf_len,
                       vmo_offset, zx_status_get_string(status));
        return zx::error(status);
      }

      ring_buffer_offset = (ring_buffer_offset + buf_len) % capacity;
      reservation_offset += buf_len;

      // Insert the "new" request, which is the latter half of the last request
      storage::BufferedOperation out_op;
      out_op.vmoid = vmoid();
      out_op.op.type = in_operations[i].op.type;
      out_op.op.vmo_offset = 0;
      out_op.op.dev_offset = dev_offset;
      out_op.op.length = buf_len;
      out_operations->push_back(std::move(out_op));
    }

    done += vmo_len;
  }

  return zx::ok(done);
}

vmoid_t RingBufferReservation::vmoid() const {
  ZX_DEBUG_ASSERT(Reserved());
  return view_.vmoid();
}

void* RingBufferReservation::Data(size_t index) {
  ZX_DEBUG_ASSERT(Reserved());
  return view_.Data(index);
}

const void* RingBufferReservation::Data(size_t index) const {
  ZX_DEBUG_ASSERT(Reserved());
  return view_.Data(index);
}

zx_status_t RingBuffer::Create(VmoidRegistry* vmoid_registry, size_t blocks, uint32_t block_size,
                               const char* label, std::unique_ptr<RingBuffer>* out) {
  VmoBuffer buffer;
  zx_status_t status = buffer.Initialize(vmoid_registry, blocks, block_size, label);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::make_unique<RingBuffer>(std::move(buffer));
  return ZX_OK;
}

RingBufferRequests::RingBufferRequests(std::vector<storage::BufferedOperation> requests,
                                       RingBufferReservation reservation)
    : requests_(std::move(requests)), reservation_(std::move(reservation)) {}

}  // namespace storage
