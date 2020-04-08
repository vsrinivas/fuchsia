// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "in_stream_buffer.h"

#include <algorithm>
#include <atomic>
#include <iostream>

InStreamBuffer::InStreamBuffer(async::Loop* fidl_loop, thrd_t fidl_thread,
                               sys::ComponentContext* component_context,
                               std::unique_ptr<InStream> in_stream_to_wrap,
                               uint64_t max_buffer_size)
    : InStream(fidl_loop, fidl_thread, component_context),
      in_stream_(std::move(in_stream_to_wrap)),
      max_buffer_size_(max_buffer_size) {
  ZX_DEBUG_ASSERT(in_stream_);
  ZX_DEBUG_ASSERT(max_buffer_size_ != 0);

  // InStreamFile knows the EOS from the start.
  PropagateEosKnown();
}

InStreamBuffer::~InStreamBuffer() {
  // nothing to do here
  // ~data_
}

zx_status_t InStreamBuffer::ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                              uint8_t* buffer_out, zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);

  uint64_t bytes_to_read = max_bytes_to_read;
  if (eos_position_known_) {
    bytes_to_read = std::min(bytes_to_read, eos_position_ - cursor_position_);
  }
  bytes_to_read = std::min(bytes_to_read, max_buffer_size_ - valid_bytes_);
  if (cursor_position_ + bytes_to_read > valid_bytes_) {
    zx_status_t status =
        ReadMoreIfPossible(cursor_position_ + bytes_to_read - valid_bytes_, just_fail_deadline);
    if (status != ZX_OK) {
      ZX_DEBUG_ASSERT(failure_seen_);
      return status;
    }
  }
  bytes_to_read = std::min(bytes_to_read, valid_bytes_ - cursor_position_);
  ZX_DEBUG_ASSERT(cursor_position_ + bytes_to_read <= valid_bytes_);
  memcpy(buffer_out, data_.data() + cursor_position_, bytes_to_read);
  *bytes_read_out = bytes_to_read;
  return ZX_OK;
}

zx_status_t InStreamBuffer::ResetToStartInternal(zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  ZX_DEBUG_ASSERT(eos_position_known_ == in_stream_->eos_position_known());
  ZX_DEBUG_ASSERT(!eos_position_known_ || eos_position_ == in_stream_->eos_position());
  cursor_position_ = 0;
  return ZX_OK;
}

zx_status_t InStreamBuffer::ReadMoreIfPossible(uint32_t bytes_to_read_if_possible,
                                               zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  ZX_DEBUG_ASSERT(bytes_to_read_if_possible != 0);
  ZX_ASSERT(max_buffer_size_ > valid_bytes_);
  ZX_DEBUG_ASSERT(eos_position_known_ == in_stream_->eos_position_known());
  ZX_DEBUG_ASSERT(!eos_position_known_ || eos_position_ == in_stream_->eos_position());
  ZX_DEBUG_ASSERT(!eos_position_known_ ||
                  valid_bytes_ + bytes_to_read_if_possible <= eos_position_);
  ZX_DEBUG_ASSERT(valid_bytes_ + bytes_to_read_if_possible <= max_buffer_size_);

  if (in_stream_->eos_position_known() &&
      (in_stream_->cursor_position() == in_stream_->eos_position())) {
    ZX_DEBUG_ASSERT(valid_bytes_ == eos_position_);
    // Not possible to read more because there isn't any more.  Not a failure.
    return ZX_OK;
  }

  // Make room.
  if (data_.size() < valid_bytes_ + bytes_to_read_if_possible) {
    // Need to resize exponentially to avoid O(N^2) overall, but cap at max_buffer_size_.
    uint64_t new_size = std::max(data_.size() * 2, static_cast<size_t>(1));
    new_size = std::max(new_size, valid_bytes_ + bytes_to_read_if_possible);
    new_size = std::min(new_size, max_buffer_size_);
    data_.resize(new_size);
  }

  uint32_t actual_bytes_read;
  zx_status_t status = in_stream_->ReadBytesShort(bytes_to_read_if_possible, &actual_bytes_read,
                                                  data_.data() + valid_bytes_, just_fail_deadline);
  if (status != ZX_OK) {
    printf("InStreamBuffer::ReadMoreIfPossible() in_stream_->ReadBytesShort() failed status: %d\n",
           status);
    ZX_DEBUG_ASSERT(!failure_seen_);
    failure_seen_ = true;
    return status;
  }

  valid_bytes_ += actual_bytes_read;

  PropagateEosKnown();
  return ZX_OK;
}

void InStreamBuffer::PropagateEosKnown() {
  if (in_stream_->eos_position_known()) {
    if (!eos_position_known_) {
      eos_position_ = in_stream_->eos_position();
      eos_position_known_ = true;
    } else {
      ZX_DEBUG_ASSERT(eos_position_ == in_stream_->eos_position());
    }
  }
  // Not intended for use in situations where whole in_stream_to_wrap doesn't fit in buffer.
  ZX_ASSERT(!eos_position_known_ || eos_position_ <= max_buffer_size_);
  ZX_ASSERT(!eos_position_known_ || valid_bytes_ <= eos_position_);
}
