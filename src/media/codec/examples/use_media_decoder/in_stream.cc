// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "in_stream.h"

#include <fbl/auto_lock.h>

#include "src/media/codec/examples/use_media_decoder/util.h"
#include "util.h"

InStream::InStream(async::Loop* fidl_loop, thrd_t fidl_thread,
                   sys::ComponentContext* component_context)
    : fidl_loop_(fidl_loop),
      fidl_dispatcher_(fidl_loop_->dispatcher()),
      fidl_thread_(fidl_thread),
      component_context_(component_context) {
  ZX_DEBUG_ASSERT(fidl_loop_);
  // Not necessarily portable, but should be valid on Zircon:
  ZX_DEBUG_ASSERT(fidl_thread_ != thrd_t{});
  ZX_DEBUG_ASSERT(component_context_);

  // For now, we don't allow construction on the fidl_thread.
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
}

InStream::~InStream() {
  // Sub-classes probably also want to check this at the start of their
  // destructor before blocking on the fidl_thread_ for anything.  That way we
  // get an assert failure instead of just getting stuck.
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
}

uint64_t InStream::cursor_position() const {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  return cursor_position_;
}

bool InStream::eos_position_known() {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  return eos_position_known_;
}

uint64_t InStream::eos_position() {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_ASSERT(eos_position_known_);
  return eos_position_;
}

zx_status_t InStream::ReadBytesShort(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                     uint8_t* buffer_out, zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  zx_status_t status =
      ReadBytesInternal(max_bytes_to_read, bytes_read_out, buffer_out, just_fail_deadline);
  if (status != ZX_OK) {
    LOGF("ReadBytesInternal failed - status: %u", status);
    failure_seen_ = true;
    return status;
  }
  cursor_position_ += *bytes_read_out;
  if (*bytes_read_out == 0) {
    bool eos_position_known_before = eos_position_known_;
    uint64_t eos_position_before = eos_position_;
    if (!eos_position_known_before) {
      eos_position_ = cursor_position_;
      eos_position_known_ = true;
    } else {
      ZX_DEBUG_ASSERT(eos_position_before == cursor_position_);
    }
  }
  return ZX_OK;
}

zx_status_t InStream::ReadBytesComplete(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                        uint8_t* buffer_out, zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!failure_seen_);
  uint32_t bytes_remaining = max_bytes_to_read;
  uint8_t* buffer_iter = buffer_out;
  while (bytes_remaining != 0) {
    uint32_t actual_bytes_read;
    zx_status_t status =
        ReadBytesShort(bytes_remaining, &actual_bytes_read, buffer_iter, just_fail_deadline);
    if (status != ZX_OK) {
      ZX_DEBUG_ASSERT(failure_seen_);
      return status;
    }
    buffer_iter += actual_bytes_read;
    bytes_remaining -= actual_bytes_read;
    if (actual_bytes_read == 0) {
      // ReadBytesShort() took care of these already.
      ZX_DEBUG_ASSERT(eos_position_known_);
      ZX_DEBUG_ASSERT(cursor_position_ == eos_position_);
      break;
    }
  }
  *bytes_read_out = buffer_iter - buffer_out;
  return ZX_OK;
}

void InStream::PostToFidlSerial(fit::closure to_run) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  PostSerial(fidl_dispatcher_, std::move(to_run));
}

void InStream::FencePostToFidlSerial() {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  FencePostSerial(fidl_dispatcher_);
}

zx_status_t InStream::ResetToStart(zx::time just_fail_deadline) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  if (cursor_position_ == 0) {
    return ZX_OK;
  }
  return ResetToStartInternal(just_fail_deadline);
}

zx_status_t InStream::ResetToStartInternal(zx::time just_fail_deadline) {
  return ZX_ERR_NOT_SUPPORTED;
};
