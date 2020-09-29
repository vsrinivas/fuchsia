// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "internal_buffer.h"

#include <lib/fit/result.h>
#include <lib/syslog/global.h>
#include <threads.h>

#include <limits>

#include <fbl/algorithm.h>

#include "src/media/lib/memory_barriers/memory_barriers.h"

#define LOG(severity, fmt, ...)                                                       \
  do {                                                                                \
    FX_LOGF(severity, nullptr, "[%s:%d] " fmt "", __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)

fit::result<InternalBuffer, zx_status_t> InternalBuffer::Create(
    const char* name, fuchsia::sysmem::AllocatorSyncPtr* sysmem, const zx::unowned_bti& bti,
    size_t size, bool is_secure, bool is_writable, bool is_mapping_needed) {
  return CreateAligned(name, sysmem, bti, size, 0, is_secure, is_writable, is_mapping_needed);
}

fit::result<InternalBuffer, zx_status_t> InternalBuffer::CreateAligned(
    const char* name, fuchsia::sysmem::AllocatorSyncPtr* sysmem, const zx::unowned_bti& bti,
    size_t size, size_t alignment, bool is_secure, bool is_writable, bool is_mapping_needed) {
  ZX_DEBUG_ASSERT(sysmem);
  ZX_DEBUG_ASSERT(*sysmem);
  ZX_DEBUG_ASSERT(*bti);
  ZX_DEBUG_ASSERT(size);
  ZX_DEBUG_ASSERT(size % ZX_PAGE_SIZE == 0);
  ZX_DEBUG_ASSERT(!is_mapping_needed || !is_secure);
  InternalBuffer local_result(size, is_secure, is_writable, is_mapping_needed);
  zx_status_t status = local_result.Init(name, sysmem, alignment, bti);
  if (status != ZX_OK) {
    LOG(ERROR, "Init() failed - status: %d", status);
    return fit::error(status);
  }
  return fit::ok(std::move(local_result));
}

InternalBuffer::~InternalBuffer() { DeInit(); }

InternalBuffer::InternalBuffer(InternalBuffer&& other)
    : size_(other.size_),
      is_secure_(other.is_secure_),
      is_writable_(other.is_writable_),
      is_mapping_needed_(other.is_mapping_needed_),
      virt_base_(other.virt_base_),
      real_size_(other.real_size_),
      real_virt_base_(other.real_virt_base_),
      alignment_offset_(other.alignment_offset_),
      pin_(std::move(other.pin_)),
      phys_base_(other.phys_base_),
      buffer_collection_(std::move(other.buffer_collection_)),
      vmo_(std::move(other.vmo_)) {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  other.is_moved_out_ = true;
}

InternalBuffer& InternalBuffer::operator=(InternalBuffer&& other) {
  // Let's just use a new variable instead of letting this happen, even though this isn't prevented
  // by C++ rules.
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(!other.is_moved_out_);
  DeInit();
  ZX_DEBUG_ASSERT(!pin_);
  size_ = other.size_;
  is_secure_ = other.is_secure_;
  is_writable_ = other.is_writable_;
  is_mapping_needed_ = other.is_mapping_needed_;
  // Let's only move instances that returned success from Init() and haven't been moved out.
  ZX_DEBUG_ASSERT(other.pin_ && !other.is_moved_out_);
  pin_ = std::move(other.pin_);
  virt_base_ = other.virt_base_;
  real_size_ = other.real_size_;
  real_virt_base_ = other.real_virt_base_;
  alignment_offset_ = other.alignment_offset_;
  phys_base_ = other.phys_base_;
  buffer_collection_ = std::move(other.buffer_collection_);
  vmo_ = std::move(other.vmo_);
  other.is_moved_out_ = true;
  return *this;
}

uint8_t* InternalBuffer::virt_base() {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(is_mapping_needed_);
  return virt_base_;
}

zx_paddr_t InternalBuffer::phys_base() {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(pin_);
  return phys_base_;
}

size_t InternalBuffer::size() {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(pin_);
  return size_;
}

const zx::vmo& InternalBuffer::vmo() {
  ZX_DEBUG_ASSERT(vmo_);
  return vmo_;
}

void InternalBuffer::CacheFlush(size_t offset, size_t length) {
  CacheFlushPossibleInvalidate(offset, length, false);
}

void InternalBuffer::CacheFlushInvalidate(size_t offset, size_t length) {
  CacheFlushPossibleInvalidate(offset, length, true);
}

void InternalBuffer::CacheFlushPossibleInvalidate(size_t offset, size_t length, bool invalidate) {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(offset <= size());
  ZX_DEBUG_ASSERT(offset + length >= offset);
  ZX_DEBUG_ASSERT(offset + length <= size());
  ZX_DEBUG_ASSERT(vmo_);
  zx_status_t status;
  if (is_secure_) {
    return;
  }
  if (invalidate) {
    BarrierBeforeInvalidate();
  }
  if (is_mapping_needed_) {
    ZX_DEBUG_ASSERT(virt_base_);
    status = zx_cache_flush(virt_base_ + offset, length,
                            ZX_CACHE_FLUSH_DATA | (invalidate ? ZX_CACHE_FLUSH_INVALIDATE : 0));
    if (status != ZX_OK) {
      ZX_PANIC("InternalBuffer::CacheFlush() zx_cache_flush() failed: %d\n", status);
    }
  } else {
    status = vmo_.op_range(ZX_VMO_OP_CACHE_CLEAN, alignment_offset_ + offset, length, nullptr, 0);
    if (status != ZX_OK) {
      ZX_PANIC("InternalBuffer::CacheFlush() op_range(CACHE_CLEAN) failed: %d", status);
    }
  }
  BarrierAfterFlush();
}

InternalBuffer::InternalBuffer(size_t size, bool is_secure, bool is_writable,
                               bool is_mapping_needed)
    : size_(size),
      is_secure_(is_secure),
      is_writable_(is_writable),
      is_mapping_needed_(is_mapping_needed) {
  ZX_DEBUG_ASSERT(size_);
  ZX_DEBUG_ASSERT(size_ % ZX_PAGE_SIZE == 0);
  ZX_DEBUG_ASSERT(!pin_);
  ZX_DEBUG_ASSERT(!is_moved_out_);
  ZX_DEBUG_ASSERT(!is_mapping_needed_ || !is_secure_);
}

zx_status_t InternalBuffer::Init(const char* name, fuchsia::sysmem::AllocatorSyncPtr* sysmem,
                                 size_t alignment, const zx::unowned_bti& bti) {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  // Init() should only be called on newly-constructed instances using a constructor other than the
  // move constructor.
  ZX_DEBUG_ASSERT(!pin_);

  // Let's interact with BufferCollection sync, since we're the only participant.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  (*sysmem)->AllocateNonSharedCollection(buffer_collection.NewRequest());

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.video = fuchsia::sysmem::videoUsageHwDecoderInternal;
  // we only want one buffer
  constraints.min_buffer_count_for_camping = 1;
  ZX_DEBUG_ASSERT(constraints.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(constraints.min_buffer_count_for_shared_slack == 0);
  ZX_DEBUG_ASSERT(constraints.min_buffer_count == 0);
  constraints.max_buffer_count = 1;

  // Allocate enough so that some portion must be aligned and large enough.
  real_size_ = size_ + alignment;
  ZX_DEBUG_ASSERT(real_size_ < std::numeric_limits<uint32_t>::max());
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = static_cast<uint32_t>(real_size_);
  constraints.buffer_memory_constraints.max_size_bytes = static_cast<uint32_t>(real_size_);
  // amlogic-video always requires contiguous; only contiguous is supported by InternalBuffer.
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.buffer_memory_constraints.secure_required = is_secure_;
  // If we need a mapping, then we don't want INACCESSIBLE domain, so we need to support at least
  // one other domain.  We choose RAM domain since InternalBuffer(s) are always used for HW DMA, and
  // we always have to CachFlush() after any write, or CacheInvalidate() before any read.  So RAM
  // domain is a better fit than CPU domain, even though we're not really sharing with any other
  // participant so the choice is less critical here.
  constraints.buffer_memory_constraints.cpu_domain_supported = false;
  constraints.buffer_memory_constraints.ram_domain_supported = is_mapping_needed_;
  // Secure buffers need support for INACCESSIBLE, and it's fine to indicate support for
  // INACCESSIBLE as long as we don't need to map, but when is_mapping_needed_ we shouldn't accept
  // INACCESSIBLE.
  //
  // Nothing presently technically stops us from mapping a buffer that's INACCESSIBLE, because MAP
  // and PIN are the same right and sysmem assumes PIN will be needed so always grants MAP, but if
  // the rights were separated, we'd potentially want to exclude MAP unless CPU/RAM domain in
  // sysmem.
  constraints.buffer_memory_constraints.inaccessible_domain_supported = !is_mapping_needed_;

  constraints.buffer_memory_constraints.heap_permitted_count = 1;
  if (is_secure_) {
    // AMLOGIC_SECURE_VDEC is only ever allocated for input buffers, never for internal buffers.
    // This is "normal" non-VDEC secure memory.  See also secmem TA's ProtectMemory / sysmem.
    constraints.buffer_memory_constraints.heap_permitted[0] =
        fuchsia::sysmem::HeapType::AMLOGIC_SECURE;
  } else {
    constraints.buffer_memory_constraints.heap_permitted[0] = fuchsia::sysmem::HeapType::SYSTEM_RAM;
  }

  // InternalBuffer(s) don't need any image format constraints, as they don't store image data.
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);

  buffer_collection->SetName(10u, name);
  buffer_collection->SetConstraints(true, std::move(constraints));

  // There's only one participant, and we've already called SetConstraints(), so this should be
  // quick.
  zx_status_t server_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 out_buffer_collection_info;
  zx_status_t status =
      buffer_collection->WaitForBuffersAllocated(&server_status, &out_buffer_collection_info);
  if (status != ZX_OK) {
    LOG(ERROR, "WaitForBuffersAllocated() failed - status: %d", status);
    return status;
  }

  if (!!is_secure_ != !!out_buffer_collection_info.settings.buffer_settings.is_secure) {
    LOG(ERROR, "sysmem bug?");
    return ZX_ERR_INTERNAL;
  }

  ZX_DEBUG_ASSERT(out_buffer_collection_info.buffers[0].vmo_usable_start % ZX_PAGE_SIZE == 0);
  zx::vmo vmo = std::move(out_buffer_collection_info.buffers[0].vmo);

  uintptr_t virt_base = 0;
  if (is_mapping_needed_) {
    zx_vm_option_t map_options = ZX_VM_PERM_READ;
    if (is_writable_) {
      map_options |= ZX_VM_PERM_WRITE;
    }

    status = zx::vmar::root_self()->map(
        /*vmar_offset=*/0, vmo, /*vmo_offset=*/0, real_size_, map_options, &virt_base);
    if (status != ZX_OK) {
      LOG(ERROR, "zx::vmar::root_self()->map() failed - status: %d", status);
      return status;
    }
  }

  uint32_t pin_options = ZX_BTI_CONTIGUOUS | ZX_BTI_PERM_READ;
  if (is_writable_) {
    pin_options |= ZX_BTI_PERM_WRITE;
  }

  zx_paddr_t phys_base;
  zx::pmt pin;
  status = bti->pin(pin_options, vmo, out_buffer_collection_info.buffers[0].vmo_usable_start,
                    real_size_, &phys_base, 1, &pin);
  if (status != ZX_OK) {
    LOG(ERROR, "BTI pin() failed - status: %d", status);
    return status;
  }

  virt_base_ = reinterpret_cast<uint8_t*>(virt_base);
  real_virt_base_ = virt_base_;
  phys_base_ = phys_base;
  if (alignment) {
    // Shift the base addresses so the physical address is aligned correctly.
    zx_paddr_t new_phys_base = fbl::round_up(phys_base, alignment);
    alignment_offset_ = new_phys_base - phys_base;
    if (is_mapping_needed_) {
      virt_base_ += alignment_offset_;
    }
    phys_base_ = new_phys_base;
  }
  pin_ = std::move(pin);
  // We keep the buffer_collection_ channel alive, but we don't listen for channel failure.  This
  // isn't ideal, since we should listen for channel failure so that sysmem can request that we
  // close the VMO handle ASAP, but so far sysmem won't try to force relinquishing buffers anyway,
  // so ... it's acceptable for now.  We keep the channel open for the lifetime of the
  // InternalBuffer so this won't look like a buffer that's pending deletion in sysmem.
  buffer_collection_ = std::move(buffer_collection);
  vmo_ = std::move(vmo);

  // Sysmem guarantees that the newly-allocated buffer starts out zeroed and cache clean, to the
  // extent possible based on is_secure.

  return ZX_OK;
}

void InternalBuffer::DeInit() {
  if (is_moved_out_) {
    return;
  }
  if (pin_) {
    pin_.unpin();
    pin_.reset();
  }
  if (virt_base_) {
    zx_status_t status =
        zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(real_virt_base_), real_size_);
    // Unmap is expected to work unless there's a bug in how we're calling it.
    ZX_ASSERT(status == ZX_OK);
    virt_base_ = nullptr;
    real_virt_base_ = nullptr;
  }
}
