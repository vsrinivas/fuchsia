// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_buffer.h"

#include "codec_impl.h"
#include "codec_port.h"

#include <lib/fxl/logging.h>

CodecBuffer::CodecBuffer(CodecImpl* parent, CodecPort port,
                         fuchsia::mediacodec::CodecBuffer buffer)
    : parent_(parent), port_(port), buffer_(std::move(buffer)) {
  // nothing else to do here
}

CodecBuffer::~CodecBuffer() {
  if (buffer_base_) {
    zx_status_t res = zx::vmar::root_self()->unmap(
        reinterpret_cast<uintptr_t>(buffer_base()), buffer_size());
    if (res != ZX_OK) {
      parent_->FailFatalLocked(
          "CodecBuffer::~Buffer() failed to unmap() Buffer");
    }
    buffer_base_ = nullptr;
  }
}

bool CodecBuffer::Init(bool input_require_write) {
  FXL_DCHECK(!input_require_write || port_ == kInputPort);
  // Map the VMO in the local address space.
  uintptr_t tmp;
  uint32_t flags = ZX_VM_FLAG_PERM_READ;
  if (port_ == kOutputPort || input_require_write) {
    flags |= ZX_VM_FLAG_PERM_WRITE;
  }
  zx_status_t res = zx::vmar::root_self()->map(
      0, buffer_.data.vmo().vmo_handle, buffer_.data.vmo().vmo_usable_start,
      buffer_.data.vmo().vmo_usable_size, flags, &tmp);
  if (res != ZX_OK) {
    printf("Failed to map %zu byte buffer vmo (res %d)\n",
           buffer_.data.vmo().vmo_usable_size, res);
    return false;
  }
  buffer_base_ = reinterpret_cast<uint8_t*>(tmp);
  return true;
}

uint64_t CodecBuffer::buffer_lifetime_ordinal() const {
  return buffer_.buffer_lifetime_ordinal;
}

uint32_t CodecBuffer::buffer_index() const { return buffer_.buffer_index; }

uint8_t* CodecBuffer::buffer_base() const {
  FXL_DCHECK(buffer_base_ && "Shouldn't be using if Init() didn't work.");
  return buffer_base_;
}

size_t CodecBuffer::buffer_size() const {
  return buffer_.data.vmo().vmo_usable_size;
}
