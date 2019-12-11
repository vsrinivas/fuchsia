// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "in_stream_peeker.h"

#include <algorithm>
#include <atomic>
#include <iostream>

InStreamPeeker::InStreamPeeker(async::Loop* fidl_loop, thrd_t fidl_thread,
                               sys::ComponentContext* component_context,
                               std::unique_ptr<InStream> in_stream_to_wrap, uint32_t max_peek_bytes)
    : InStream(fidl_loop, fidl_thread, component_context),
      in_stream_(std::move(in_stream_to_wrap)),
      max_peek_bytes_(std::max(1u, max_peek_bytes)) {
  ZX_DEBUG_ASSERT(in_stream_);
  // We force max_peek_bytes_ to be at least 1 above to avoid edge cases.
  ZX_DEBUG_ASSERT(max_peek_bytes_ != 0);

  // We don't really need PAGE_SIZE, since we can just create the VMO first,
  // let it round its own size up to PAGE_SIZE boundary, then double that for
  // the size of the child vmar.
  //
  // Non-resizable just because we can, and because resizable would not make
  // sense for this.
  zx_status_t status = zx::vmo::create(max_peek_bytes_, 0, &ring_vmo_);
  ZX_ASSERT(status == ZX_OK);

  status = ring_vmo_.get_size(&vmo_size_bytes_);
  ZX_ASSERT(status == ZX_OK);

  // Set up a VA-contiguous double-mapping of a ring buffer.
  //
  // The ring_vmar_ is 2x the size of ring_vmo_, to make room to double-map the
  // ring_vmo_.
  //
  // First create a child VMAR that'll have room and that has
  // ZX_VM_CAN_MAP_SPECIFIC.
  status = zx::vmar::root_self()->allocate(
      0, vmo_size_bytes_ * 2, ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
      &ring_vmar_, reinterpret_cast<uintptr_t*>(&ring_base_));
  ZX_ASSERT(status == ZX_OK);

  // Now we can map the ring_vmo_ twice, adjacently.  This allows us to use
  // ranges that span past the end of the ring_vmo_ back to the start of the
  // ring_vmo_, without needing to split the range up manually.
  //
  // We don't really need ptr, since we already have ring_base_.  But we can
  // assert that ptr is what we expect each time.
  const zx_vm_option_t kMapOptions =
      ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE;
  uintptr_t ptr;
  status = ring_vmar_.map(0, ring_vmo_, 0, vmo_size_bytes_, kMapOptions, &ptr);
  ZX_ASSERT(status == ZX_OK);
  ZX_ASSERT(reinterpret_cast<uint8_t*>(ptr) == ring_base_);
  status = ring_vmar_.map(vmo_size_bytes_, ring_vmo_, 0, vmo_size_bytes_, kMapOptions, &ptr);
  ZX_ASSERT(status == ZX_OK);
  ZX_ASSERT(reinterpret_cast<uint8_t*>(ptr) == ring_base_ + vmo_size_bytes_);

  // InStreamFile knows the EOS from the start.
  PropagateEosKnown();
}

InStreamPeeker::~InStreamPeeker() {
  // Just closing the handle doesn't free up the ring's VA space.  But destroy()
  // does.
  ring_vmar_.destroy();
}

uint32_t InStreamPeeker::max_peek_bytes() { return max_peek_bytes_; }

zx_status_t InStreamPeeker::PeekBytes(uint32_t desired_bytes_to_peek, uint32_t* peek_bytes_out,
                                      uint8_t** peek_buffer_out, zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  ZX_DEBUG_ASSERT(desired_bytes_to_peek <= max_peek_bytes_);
  ZX_DEBUG_ASSERT(max_peek_bytes_ <= vmo_size_bytes_);
  ZX_DEBUG_ASSERT(!eos_position_known_ || cursor_position_ + valid_bytes_ <= eos_position_);
  ZX_DEBUG_ASSERT(peek_bytes_out);
  ZX_DEBUG_ASSERT(peek_buffer_out);
  // After a failure, don't call this method again.
  ZX_DEBUG_ASSERT(in_stream_);
  zx_status_t status;
  if (desired_bytes_to_peek > valid_bytes_) {
    status = ReadMoreIfPossible(desired_bytes_to_peek - valid_bytes_, just_fail_deadline);
    if (status != ZX_OK) {
      ZX_DEBUG_ASSERT(failure_seen_);
      return status;
    }
  }
  ZX_DEBUG_ASSERT(desired_bytes_to_peek <= valid_bytes_ ||
                  (eos_position_known_ && cursor_position_ + valid_bytes_ == eos_position_));
  ZX_DEBUG_ASSERT(read_offset_ < vmo_size_bytes_);
  ZX_DEBUG_ASSERT(write_offset_ < vmo_size_bytes_);
  *peek_buffer_out = ring_base_ + read_offset_;
  *peek_bytes_out = std::min(desired_bytes_to_peek, valid_bytes_);
  return ZX_OK;
}

void InStreamPeeker::TossPeekedBytes(uint32_t bytes_to_toss) {
  // If they were peeked and not already tossed since, then they're still bytes
  // counted by valid_bytes_.
  ZX_DEBUG_ASSERT(bytes_to_toss <= valid_bytes_);
  read_offset_ = (read_offset_ + bytes_to_toss) % vmo_size_bytes_;
  valid_bytes_ -= bytes_to_toss;
  cursor_position_ += bytes_to_toss;
}

zx_status_t InStreamPeeker::ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                              uint8_t* buffer_out, zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  // If the ring has any data, satisfy from there, else satisfy directly from
  // in_stream_.  Don't bother stitching together the two, as callers are still
  // expected to handle short reads anyway.
  if (valid_bytes_ != 0) {
    uint64_t bytes_to_read = std::min(valid_bytes_, max_bytes_to_read);
    // We go ahead and promise that previosly-peeked bytes can be read without
    // short reads, since there's no downside to making that promise.
    memcpy(buffer_out, ring_base_ + read_offset_, bytes_to_read);
    read_offset_ = (read_offset_ + bytes_to_read) % vmo_size_bytes_;
    valid_bytes_ -= bytes_to_read;
    *bytes_read_out = bytes_to_read;
    return ZX_OK;
  } else {
    ZX_DEBUG_ASSERT(valid_bytes_ == 0);
    // In this case we bypass the ring until another peek happens.  This means
    // the correspondence between ring offsets and cursor_position_ is decoupled
    // until then, which in turn means we can't assert that cursor_position_ and
    // read_offset_ have any particular relationship (for example).
    //
    // We can assert in this case that the cursor_position()s match though.
    ZX_DEBUG_ASSERT(cursor_position_ == in_stream_->cursor_position());
    zx_status_t status = in_stream_->ReadBytesShort(max_bytes_to_read, bytes_read_out, buffer_out,
                                                    just_fail_deadline);
    if (status != ZX_OK) {
      ZX_DEBUG_ASSERT(!failure_seen_);
      failure_seen_ = true;
      return status;
    }
    PropagateEosKnown();
    return ZX_OK;
  }
}

zx_status_t InStreamPeeker::ReadMoreIfPossible(uint32_t bytes_to_read_if_possible,
                                               zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  ZX_DEBUG_ASSERT(bytes_to_read_if_possible != 0);
  ZX_DEBUG_ASSERT(valid_bytes_ + bytes_to_read_if_possible <= max_peek_bytes_);
  ZX_DEBUG_ASSERT(max_peek_bytes_ <= vmo_size_bytes_);
  ZX_DEBUG_ASSERT(read_offset_ < vmo_size_bytes_);
  ZX_DEBUG_ASSERT(write_offset_ < vmo_size_bytes_);

  ZX_DEBUG_ASSERT(eos_position_known_ == in_stream_->eos_position_known());
  ZX_DEBUG_ASSERT(!eos_position_known_ || eos_position_ == in_stream_->eos_position());

  if (in_stream_->eos_position_known() &&
      (in_stream_->cursor_position() == in_stream_->eos_position())) {
    ZX_DEBUG_ASSERT(cursor_position_ + valid_bytes_ == eos_position_);
    // Not possible to read more because there isn't any more.  Not a failure.
    return ZX_OK;
  }

  // Thanks to release semantics, reads from other mapping syntactically above
  // this must be done before this.
  //
  // Thanks to acquire semantics, the write into the ring syntactially below
  // must be done after this.
  ring_memory_fence_.fetch_add(1, std::memory_order_acq_rel);

  uint32_t actual_bytes_read;
  zx_status_t status = in_stream_->ReadBytesComplete(bytes_to_read_if_possible, &actual_bytes_read,
                                                     ring_base_ + write_offset_);
  if (status != ZX_OK) {
    ZX_DEBUG_ASSERT(!failure_seen_);
    failure_seen_ = true;
    return status;
  }

  // Thanks to release semantics, the write into ring via one mapping syntactically above must be
  // done before this.
  //
  // Thanks to acqurie semantics, the reads from other mapping syntactically below this must be
  // after this.
  ring_memory_fence_.fetch_add(1, std::memory_order_acq_rel);

  write_offset_ = (write_offset_ + actual_bytes_read) % vmo_size_bytes_;
  valid_bytes_ += actual_bytes_read;

  PropagateEosKnown();
  return ZX_OK;
}

void InStreamPeeker::PropagateEosKnown() {
  if (in_stream_->eos_position_known()) {
    if (!eos_position_known_) {
      eos_position_ = in_stream_->eos_position();
      eos_position_known_ = true;
    } else {
      ZX_DEBUG_ASSERT(eos_position_ == in_stream_->eos_position());
    }
  }
}
