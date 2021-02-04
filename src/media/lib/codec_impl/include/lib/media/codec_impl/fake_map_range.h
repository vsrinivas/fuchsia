// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FAKE_MAP_RANGE_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FAKE_MAP_RANGE_H_

#include <lib/stdcompat/optional.h>
#include <lib/zx/vmar.h>
#include <stddef.h>
#include <zircon/types.h>

// We create a fake_map_vmar_ when the allocated buffers are secure, as part of minimizing the
// code differences between non-secure memory and secure memory.  The CodecBuffer::buffer_base()
// will return the fake_map_addr_, and data pointers can still be meaningful in terms of their
// distance from buffer_base() of their buffer (depending on CodecAdapter implementation).  We
// only create one vmar since we don't really need one per buffer.  Doing this also robustly
// detects any CodecAdapter code that's trying to directly access buffer contents despite the
// buffer being secure memory (without the read getting stuck, and without aarch64 speculative
// execution potentially creating spurious faults IIUC).  We never actaully map a secure buffer
// VMO, but we do fake it using these fields.
//
// TODO(dustingreen): Depending on the cost in kernel vaddr range tracking resources, if it
// becomes safe to just map a secure VMO (in the sense that faults would work and only occur if
// actually touched), we could get rid of this fake map stuff and just let the secure VMOs be
// mapped.  They'd still not actually be touched unless there's a bug, so hopefully page table
// resources wouldn't be consumed (except transiently in faulting if there's a bug in a
// CodecAdapter where it tries to directly touch a secure VMO via buffer_base()).  This fake map
// stuff is a workaround due to not being able to set uncached policy on contig VMOs to mitigate
// spurious faults via cached mapping / writes that seem to complete instead of faulting + reads
// from secure VMO mapping getting stuck instead of getting a process-visible fault.
class FakeMapRange {
 public:
  // The specified size need not account for extra VA space needed in case of buffers that aren't
  // aligned with respect to ZX_PAGE_SIZE.  This class provides that extra space automatically.
  //
  // Create() will assert if result isn't empty.
  static zx_status_t Create(size_t size, cpp17::optional<FakeMapRange>* result);
  ~FakeMapRange();

  // move only; no copy ("delete" here just to make it explicit)
  FakeMapRange(FakeMapRange&& other);
  FakeMapRange& operator=(FakeMapRange&& other);
  FakeMapRange(const FakeMapRange& other) = delete;
  FakeMapRange& operator=(const FakeMapRange& other) = delete;

  // Attempts to read or write memory via base() will intentionally fault.
  //
  // The returned address is always ZX_PAGE_SIZE aligned.
  //
  // The returned address has enough room to accomodate a fake buffer base pointer that preserves
  // low-order page-offset bits for a buffer with any alignment with respect to ZX_PAGE_SIZE.
  uint8_t* base();
  // This size is how large a buffer can be supported by this instance.
  size_t size();

 private:
  explicit FakeMapRange(size_t size);
  zx_status_t Init();
  size_t raw_size_ = 0;
  size_t vmar_size_ = 0;
  zx::vmar vmar_;
  uintptr_t vmar_addr_ = reinterpret_cast<uintptr_t>(nullptr);
  bool is_ready_ = false;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FAKE_MAP_RANGE_H_
