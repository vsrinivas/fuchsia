// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_impl.h>
#include <lib/media/codec_impl/codec_port.h>
#include <lib/media/codec_impl/log.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <src/media/lib/memory_barriers/memory_barriers.h>

CodecBuffer::CodecBuffer(CodecImpl* parent, Info buffer_info, CodecVmoRange vmo_range)
    : parent_(parent), buffer_info_(std::move(buffer_info)), vmo_range_(std::move(vmo_range)) {
  // nothing else to do here
}

CodecBuffer::~CodecBuffer() {
  zx_status_t status;
  if (is_mapped_) {
    ZX_DEBUG_ASSERT(buffer_base_);
    uintptr_t unmap_address = fbl::round_down(reinterpret_cast<uintptr_t>(base()), ZX_PAGE_SIZE);
    size_t unmap_len =
        fbl::round_up(reinterpret_cast<uintptr_t>(base() + size()), ZX_PAGE_SIZE) - unmap_address;
    status = zx::vmar::root_self()->unmap(unmap_address, unmap_len);
    if (status != ZX_OK) {
      parent_->FailFatalLocked("CodecBuffer::~CodecBuffer() failed to unmap() Buffer - status: %d",
                               status);
    }
    buffer_base_ = nullptr;
    is_mapped_ = false;
  }
  if (pinned_) {
    status = pinned_.unpin();
    if (status != ZX_OK) {
      parent_->FailFatalLocked("CodecBuffer::~CodecBuffer() failed unpin() - status: %d", status);
    }
  }
}

bool CodecBuffer::Map() {
  ZX_DEBUG_ASSERT(!buffer_info_.is_secure);
  // Map the VMO in the local address space.
  uintptr_t tmp;
  zx_vm_option_t flags = ZX_VM_PERM_READ;
  if (buffer_info_.port == kOutputPort) {
    flags |= ZX_VM_PERM_WRITE;
  }

  // We must page-align the mapping (since HW can only map at page granularity).  This means the
  // mapping may include up to ZX_PAGE_SIZE - 1 bytes before vmo_usable_start, and up to
  // ZX_PAGE_SIZE - 1 bytes after vmo_usable_start + vmo_usable_size.  The usage of the mapping is
  // expected to stay within CodecBuffer::base() to CodecBuffer::base() + vmo_usable_size.
  uint64_t adjusted_vmo_offset = fbl::round_down(vmo_offset(), ZX_PAGE_SIZE);
  size_t len = fbl::round_up(vmo_offset() + size(), ZX_PAGE_SIZE) - adjusted_vmo_offset;
  zx_status_t res = zx::vmar::root_self()->map(0, vmo(), adjusted_vmo_offset, len, flags, &tmp);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to map %zu byte buffer vmo (res %d)", size(), res);
    return false;
  }
  buffer_base_ = reinterpret_cast<uint8_t*>(tmp + (vmo_offset() % ZX_PAGE_SIZE));
  is_mapped_ = true;
  return true;
}

void CodecBuffer::FakeMap(uint8_t* fake_map_addr) {
  ZX_DEBUG_ASSERT(reinterpret_cast<uintptr_t>(fake_map_addr) % ZX_PAGE_SIZE == 0);
  buffer_base_ = fake_map_addr + (vmo_offset() % ZX_PAGE_SIZE);
  ZX_DEBUG_ASSERT(!is_mapped_);
}

uint8_t* CodecBuffer::base() const {
  ZX_DEBUG_ASSERT(buffer_base_ && "Shouldn't be using if buffer was not mapped.");
  return buffer_base_;
}

bool CodecBuffer::is_known_contiguous() const { return is_known_contiguous_; }

zx_paddr_t CodecBuffer::physical_base() const {
  // Must call Pin() first.
  ZX_DEBUG_ASSERT(pinned_);
  // Else we'll need a different method that can deal with scattered pages.  For now we don't need
  // that.
  ZX_DEBUG_ASSERT(is_known_contiguous_);
  return contiguous_paddr_base_;
}

size_t CodecBuffer::size() const { return vmo_range_.size(); }

const zx::vmo& CodecBuffer::vmo() const { return vmo_range_.vmo(); }

uint64_t CodecBuffer::vmo_offset() const { return vmo_range_.offset(); }

void CodecBuffer::SetVideoFrame(std::weak_ptr<VideoFrame> video_frame) const {
  video_frame_ = video_frame;
}

std::weak_ptr<VideoFrame> CodecBuffer::video_frame() const { return video_frame_; }

zx_status_t CodecBuffer::Pin() {
  if (is_pinned()) {
    return ZX_OK;
  }

  zx_info_vmo_t info;
  zx_status_t status = vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (!(info.flags & ZX_INFO_VMO_CONTIGUOUS)) {
    // Not supported yet.
    return ZX_ERR_NOT_SUPPORTED;
  }
  // We could potentially know this via the BufferCollectionInfo_2, but checking the VMO directly
  // also works fine.
  is_known_contiguous_ = true;

  // We must page-align the pin (since pining is page granularity).  This means the pin may include
  // up to ZX_PAGE_SIZE - 1 bytes before vmo_usable_start, and up to ZX_PAGE_SIZE - 1 bytes after
  // vmo_usable_start + vmo_usable_size. The usage of the pin is expected to stay within
  // CodecBuffer::base() to CodecBuffer::base() + vmo_usable_size.
  uint64_t pin_offset = fbl::round_down(vmo_offset(), ZX_PAGE_SIZE);
  uint64_t pin_size = fbl::round_up(vmo_offset() + size(), ZX_PAGE_SIZE) - pin_offset;

  uint32_t options = ZX_BTI_CONTIGUOUS | ZX_BTI_PERM_READ;
  if (port() == kOutputPort) {
    options |= ZX_BTI_PERM_WRITE;
  }

  zx_paddr_t paddr;
  status = parent_->Pin(options, vmo(), pin_offset, pin_size, &paddr, 1, &pinned_);
  if (status != ZX_OK) {
    return status;
  }
  // Include the low-order bits of vmo_usable_start() in contiguous_paddr_base_ so that the paddr
  // at contiguous_paddr_base_ points (physical) at the byte at offset vmo_usable_start() within
  // *vmo.
  contiguous_paddr_base_ = paddr + (vmo_offset() % ZX_PAGE_SIZE);
  return ZX_OK;
}

bool CodecBuffer::is_pinned() const { return !!pinned_; }

void CodecBuffer::CacheFlush(uint32_t flush_offset, uint32_t length) const {
  CacheFlushInternal(flush_offset, length, /*also_invalidate*/ false);
}

void CodecBuffer::CacheFlushAndInvalidate(uint32_t flush_offset, uint32_t length) const {
  CacheFlushInternal(flush_offset, length, /*also_invalidate=*/true);
}

void CodecBuffer::CacheFlushInternal(uint32_t flush_offset, uint32_t length,
                                     bool also_invalidate) const {
  ZX_DEBUG_ASSERT(!is_secure());
  if (also_invalidate) {
    BarrierBeforeInvalidate();
  }
  zx_status_t status;
  if (is_mapped_) {
    uint32_t flush_type = also_invalidate ? ZX_CACHE_FLUSH_INVALIDATE : ZX_CACHE_FLUSH_DATA;
    status = zx_cache_flush(base() + flush_offset, length, flush_type);
    if (status != ZX_OK) {
      ZX_PANIC("zx_cache_flush() failed - status: %d", status);
    }
  } else {
    uint32_t flush_type =
        also_invalidate ? ZX_VMO_OP_CACHE_CLEAN_INVALIDATE : ZX_VMO_OP_CACHE_CLEAN;
    status = vmo().op_range(flush_type, vmo_offset() + flush_offset, length, nullptr, 0);
    if (status != ZX_OK) {
      ZX_PANIC("vmo().op_range() failed - status: %d", status);
    }
  }
  BarrierAfterFlush();
}
