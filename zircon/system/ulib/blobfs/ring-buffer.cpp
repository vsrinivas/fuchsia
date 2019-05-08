// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/ring-buffer.h>

#include <utility>

#include <fbl/auto_lock.h>

namespace blobfs {

zx_status_t internal::RingBufferState::Reserve(uint64_t blocks, RingBufferReservation* out) {
    ZX_DEBUG_ASSERT(blocks > 0);
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
                pending_free_.insert(i, Range { reservation.start(), reservation.length()});
                return;
            }
        }
        pending_free_.push_back(Range { reservation.start(), reservation.length() });
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
        FS_TRACE_WARN("blobfs: Requested reservation too large (%zu > %zu blocks)\n",
                      blocks, capacity());
    }
    return reserved_length_ + blocks <= capacity();
}

void internal::RingBufferState::CompleteFreeLocked(size_t start, size_t blocks) {
    ZX_DEBUG_ASSERT_MSG(start == reserved_start_, "Freeing out-of-order");
    reserved_start_ = (reserved_start_ + blocks) % capacity();
    reserved_length_ -= blocks;
}

RingBufferReservation::RingBufferReservation(internal::RingBufferState* buffer,
                                             size_t start, size_t length)
    : buffer_(buffer), start_(start), length_(length) {}

RingBufferReservation::RingBufferReservation(RingBufferReservation&& other)
    : buffer_(other.buffer_),
      start_(other.start_),
      length_(other.length_) {
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
    start_ = other.start_;
    length_ = other.length_;
    other.buffer_ = nullptr;
    other.Reset();
    ZX_DEBUG_ASSERT(!other.Reserved());
    return *this;
}

RingBufferReservation::~RingBufferReservation() {
    Reset();
}

void RingBufferReservation::Reset() {
    if (Reserved()) {
        buffer_->Free(*this);
    }
    buffer_ = nullptr;
    start_ = 0;
    length_ = 0;
}

zx_status_t RingBufferReservation::CopyRequests(
        const fbl::Vector<UnbufferedOperation>& in_operations, size_t offset,
        fbl::Vector<BufferedOperation>* out) {
    ZX_DEBUG_ASSERT_MSG(Reserved(), "Copying to invalid reservation");
    fbl::Vector<BufferedOperation> out_operations;
    out_operations.reserve(in_operations.size());

    ZX_DEBUG_ASSERT_MSG(offset + BlockCount(in_operations) <= length(),
                        "Copying requests into a buffer beyond limit of prior reservation");

    const size_t capacity = buffer_->capacity();
    // Offset into this reservation.
    size_t reservation_offset = offset;
    // Offset into the target ring buffer.
    size_t ring_buffer_offset = (start() + reservation_offset) % capacity;

    for (size_t i = 0; i < in_operations.size(); i++) {
        // Read parameters of the current request.
        ZX_DEBUG_ASSERT_MSG(in_operations[i].op.type == OperationType::kWrite,
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
        void* ptr = MutableData(reservation_offset);

        zx_status_t status = vmo->read(ptr, vmo_offset * kBlobfsBlockSize,
                                       buf_len * kBlobfsBlockSize);
        if (status != ZX_OK) {
            return status;
        }

        BufferedOperation out_op;
        out_op.vmoid = vmoid();
        out_op.op.type = in_operations[i].op.type;
        out_op.op.vmo_offset = ring_buffer_offset;
        out_op.op.dev_offset = dev_offset;
        out_op.op.length = buf_len;
        out_operations.push_back(std::move(out_op));

        ring_buffer_offset = (ring_buffer_offset + buf_len) % capacity;
        reservation_offset += buf_len;

        if (buf_len != vmo_len) {
            // We wrapped around; write what remains from this request.
            vmo_offset += buf_len;
            dev_offset += buf_len;
            buf_len = vmo_len - buf_len;
            ZX_DEBUG_ASSERT(buf_len > 0);

            ptr = MutableData(reservation_offset);
            status = vmo->read(ptr, vmo_offset * kBlobfsBlockSize, buf_len * kBlobfsBlockSize);
            if (status != ZX_OK) {
                return status;
            }

            ring_buffer_offset = (ring_buffer_offset + buf_len) % capacity;
            reservation_offset += buf_len;

            // Insert the "new" request, which is the latter half of the last request
            BufferedOperation out_op;
            out_op.vmoid = vmoid();
            out_op.op.type = in_operations[i].op.type;
            out_op.op.vmo_offset = 0;
            out_op.op.dev_offset = dev_offset;
            out_op.op.length = buf_len;
            out_operations.push_back(std::move(out_op));
        }
    }

    *out = std::move(out_operations);
    return ZX_OK;
}

vmoid_t RingBufferReservation::vmoid() const {
    ZX_DEBUG_ASSERT(Reserved());
    return buffer_->vmoid();
}

void* RingBufferReservation::MutableData(size_t index) {
    ZX_DEBUG_ASSERT(Reserved());
    ZX_DEBUG_ASSERT_MSG(index < length_, "Accessing data outside the current reservation");
    return buffer_->MutableData((start_ + index) % buffer_->capacity());
}

zx_status_t RingBuffer::Create(SpaceManager* space_manager, size_t blocks, const char* label,
                               std::unique_ptr<RingBuffer>* out) {
    VmoBuffer buffer;
    zx_status_t status = buffer.Initialize(space_manager, blocks, label);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("RingBuffer: Failed to create internal buffer\n");
        return status;
    }

    *out = std::make_unique<RingBuffer>(std::move(buffer));
    return ZX_OK;
}

RingBufferRequests::RingBufferRequests(fbl::Vector<BufferedOperation> requests,
                                       RingBufferReservation reservation)
    : requests_(std::move(requests)), reservation_(std::move(reservation)) {}

} // namespace blobfs
