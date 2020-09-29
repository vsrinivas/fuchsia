// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_INTERNAL_BUFFER_INTERNAL_BUFFER_H_
#define SRC_MEDIA_LIB_INTERNAL_BUFFER_INTERNAL_BUFFER_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/function.h>
#include <threads.h>
#include <zircon/types.h>

// All amlogic-video InternalBuffer(s) are phyiscally contiguous.  All are allocated via sysmem in
// fuchsia::sysmem::HeapType::SYSTEM_RAM or fuchsia::sysmem::HeapType::AMLOGIC_SECURE memory,
// depending on whether is_secure.
class InternalBuffer {
 public:
  using ErrorHandler = fit::callback<void(zx_status_t status)>;
  // |name| is borrowed during the call - not retained.  Copied into ZX_PROP_NAME of the allocated
  // vmo.
  //
  // |sysmem| is borrowed during the call - not retained.
  //
  // |bti| is borrowed during the call - not retained.
  //
  // |size| of the requested buffer.  This must be % ZX_PAGE_SIZE == 0.
  //
  // |is_secure| is whether to allocate secure buffers or non-secure buffers.  All buffers are
  // allocated via sysmem and are physically contiguous.
  //
  // |is_writable| the buffer must be writable, else read-only.
  //
  // |is_mapping_needed| if a mapping to the allocated buffer is needed.  This must be false if
  // is_secure.
  static fit::result<InternalBuffer, zx_status_t> Create(const char* name,
                                                         fuchsia::sysmem::AllocatorSyncPtr* sysmem,
                                                         const zx::unowned_bti& bti, size_t size,
                                                         bool is_secure, bool is_writable,
                                                         bool is_mapping_needed);

  // Same as above, but alignment is the byte multiple to align the buffer to.
  static fit::result<InternalBuffer, zx_status_t> CreateAligned(
      const char* name, fuchsia::sysmem::AllocatorSyncPtr* sysmem, const zx::unowned_bti& bti,
      size_t size, size_t alignment, bool is_secure, bool is_writable, bool is_mapping_needed);

  ~InternalBuffer();

  // move-only; delete copy just for clarity
  InternalBuffer(InternalBuffer&& other);
  InternalBuffer& operator=(InternalBuffer&& other);
  InternalBuffer(const InternalBuffer& other) = delete;
  InternalBuffer& operator=(const InternalBuffer& other) = delete;

  // This will assert in debug if !is_mapping_needed.
  uint8_t* virt_base();

  zx_paddr_t phys_base();

  size_t size();

  // If is_secure, ignored.  If !is_secure, flushes cache, or panics if the flush doesn't work.
  //
  // offset - start of range to flush
  // length - length of range to flush
  void CacheFlush(size_t offset, size_t length);
  void CacheFlushInvalidate(size_t offset, size_t length);

 private:
  InternalBuffer(size_t size, bool is_secure, bool is_writable, bool is_mapping_needed);

  InternalBuffer(size_t size);
  zx_status_t Init(const char* name, fuchsia::sysmem::AllocatorSyncPtr* sysmem, size_t alignment,
                   const zx::unowned_bti& bti);
  void DeInit();
  void CacheFlushPossibleInvalidate(size_t offset, size_t length, bool invalidate);

  size_t size_{};
  bool is_secure_{};
  bool is_writable_{};
  bool is_mapping_needed_{};
  uint8_t* virt_base_{};
  // Include size for alignment.
  size_t real_size_{};
  uint8_t* real_virt_base_{};
  uintptr_t alignment_offset_{};
  zx::pmt pin_;
  zx_paddr_t phys_base_{};
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> buffer_collection_;
  zx::vmo vmo_;
  bool is_moved_out_ = false;
};

#endif  // SRC_MEDIA_LIB_INTERNAL_BUFFER_INTERNAL_BUFFER_H_
