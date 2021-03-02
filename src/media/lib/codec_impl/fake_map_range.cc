// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/fake_map_range.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <fbl/algorithm.h>

zx_status_t FakeMapRange::Create(size_t size, cpp17::optional<FakeMapRange>* result_param) {
  cpp17::optional<FakeMapRange>& result = *result_param;
  // We can't emplace without making more constructors public or adding brittle friending of
  // optional implementation details, so instead we create one locally and move it in, since it's
  // fine if the move constructor is public. This also avoids putting anything in *result_param
  // until we know we'll return ZX_OK.
  FakeMapRange local_result(size);
  zx_status_t status = local_result.Init();
  if (status != ZX_OK) {
    return status;
  }
  result.emplace(std::move(local_result));
  return ZX_OK;
}

FakeMapRange::~FakeMapRange() {
  // Explicitly destroy(), else the kernel intentionally keeps the VMAR's vaddr range despite the
  // zx::vmar::~vmar closing the handle.
  if (vmar_) {
    vmar_.destroy();
  }
}

FakeMapRange::FakeMapRange(FakeMapRange&& other)
    : raw_size_(other.raw_size_),
      vmar_size_(other.vmar_size_),
      vmar_(std::move(other.vmar_)),
      vmar_addr_(other.vmar_addr_),
      is_ready_(other.is_ready_) {
  // Helps detect usage after moving out.
  other.is_ready_ = false;
}

FakeMapRange& FakeMapRange::operator=(FakeMapRange&& other) {
  if (vmar_) {
    vmar_.destroy();
  }
  raw_size_ = other.raw_size_;
  vmar_size_ = other.vmar_size_;
  vmar_ = std::move(other.vmar_);
  vmar_addr_ = other.vmar_addr_;
  is_ready_ = other.is_ready_;
  other.is_ready_ = false;
  return *this;
}

uint8_t* FakeMapRange::base() {
  ZX_DEBUG_ASSERT(is_ready_);
  ZX_DEBUG_ASSERT(vmar_addr_);
  return reinterpret_cast<uint8_t*>(vmar_addr_);
}

size_t FakeMapRange::size() {
  // Require that size() only be called after Init() since we can.
  ZX_DEBUG_ASSERT(is_ready_);
  return raw_size_;
}

FakeMapRange::FakeMapRange(size_t size) : raw_size_(size) {
  ZX_DEBUG_ASSERT(raw_size_);

  // The worst-case required vmar_size_ will be when this instance is used for a buffer where
  // vmo_usable_start() % ZX_PAGE_SIZE == ZX_PAGE_SIZE - 1.  In that case, we need a whole page just
  // for the first byte, and also the rest of the page containing the last byte.

  vmar_size_ = fbl::round_up(ZX_PAGE_SIZE - 1 + raw_size_, ZX_PAGE_SIZE);
  ZX_DEBUG_ASSERT(vmar_size_ % ZX_PAGE_SIZE == 0);
  ZX_DEBUG_ASSERT(ZX_PAGE_SIZE - 1 + raw_size_ <= vmar_size_);
}

zx_status_t FakeMapRange::Init() {
  // We don't intend to map anything in the VMAR, so don't need ZX_VM_CAN_MAP_READ or WRITE.
  constexpr uint32_t kAllocateOptions = 0;
  zx_status_t status =
      zx::vmar::root_self()->allocate(kAllocateOptions, 0, vmar_size_, &vmar_, &vmar_addr_);
  if (status != ZX_OK) {
    printf("zx::vmar::root_self()->allocate() failed - status: %d\n", status);
    return status;
  }
  // We don't need to call vmar::protect(0), because a VMAR without any sub-regions already faults
  // on any access.
  is_ready_ = true;
  return ZX_OK;
}
