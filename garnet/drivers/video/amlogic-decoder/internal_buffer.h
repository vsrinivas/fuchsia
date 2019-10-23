// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <threads.h>
#include <zircon/types.h>

// All amlogic-video InternalBuffer(s) are phyiscally contiguous.  All are allocated via sysmem in
// fuchsia::sysmem::HeapType::SYSTEM_RAM or fuchsia::sysmem::HeapType::AMLOGIC_SECURE memory,
// depending on whether is_secure.
class InternalBuffer {
 public:
  using ErrorHandler = fit::callback<void(zx_status_t status)>;
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
  static fit::result<InternalBuffer, zx_status_t> Create(
      fuchsia::sysmem::AllocatorSyncPtr* sysmem, zx::bti* bti, size_t size, bool is_secure,
      bool is_writable, bool is_mapping_needed);

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

 private:
  InternalBuffer(size_t size, bool is_secure, bool is_writable, bool is_mapping_needed);

  InternalBuffer(size_t size);
  zx_status_t Init(fuchsia::sysmem::AllocatorSyncPtr* sysmem, zx::bti* bti);
  void DeInit();

  size_t size_{};
  bool is_secure_{};
  bool is_writable_{};
  bool is_mapping_needed_{};
  uint8_t* virt_base_{};
  zx::pmt pin_;
  zx_paddr_t phys_base_{};
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> buffer_collection_;
  zx::vmo vmo_;
  bool is_moved_out_ = false;
};
