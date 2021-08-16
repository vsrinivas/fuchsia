// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmar.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <limits>

#include <fbl/algorithm.h>

namespace fuzzing {
namespace {

// These are the flags that the shared memory should be mapped with.
const zx_vm_option_t kOptions =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE | ZX_VM_REQUIRE_NON_RESIZABLE;

// If size is |kInlinedSize|, buffer starts with an inline header.
struct InlineHeader {
  char magic[8];
  uint64_t size;
};
const char* kInlineMagic = "INLINED";
const size_t kInlinedSize = std::numeric_limits<size_t>::max();

// Returns a page-aligned capacity, taking into accound the extra space need for the inlined size.
size_t ActualCapacity(size_t capacity, size_t size) {
  return capacity + (size == kInlinedSize ? sizeof(InlineHeader) : 0);
}

size_t AlignedCapacity(size_t capacity, size_t size) {
  return fbl::round_up(ActualCapacity(capacity, size), ZX_PAGE_SIZE);
}

}  // namespace

// Public methods

SharedMemory& SharedMemory::operator=(SharedMemory&& other) {
  vmo_ = std::move(other.vmo_);
  addr_ = other.addr_;
  capacity_ = other.capacity_;
  source_ = other.source_;
  size_ = other.size_;
  // Set |other.addr_| to 0 to prevent |Reset| from unmapping the VMO.
  other.addr_ = 0;
  other.Reset();
  return *this;
}

SharedMemory::~SharedMemory() { Reset(); }

void SharedMemory::Create(size_t capacity, Buffer* out, bool inline_size) {
  Reset();
  capacity_ = capacity;
  size_ = inline_size ? kInlinedSize : 0;
  auto aligned_capacity = AlignedCapacity(capacity_, size_);
  zx_status_t status = zx::vmo::create(aligned_capacity, 0, &vmo_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create VMO: " << zx_status_get_string(status);
  }
  status = zx::vmar::root_self()->map(kOptions, 0, vmo_, 0, aligned_capacity, &addr_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to map VMO: " << zx_status_get_string(status);
  }
  status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &out->vmo);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to duplicate VMO: " << zx_status_get_string(status);
  }
  out->size = ActualCapacity(capacity_, size_);
  if (inline_size) {
    auto* header = reinterpret_cast<InlineHeader*>(addr_);
    strncpy(header->magic, kInlineMagic, sizeof(header->magic));
    header->size = 0;
  }
}

void SharedMemory::Share(const void* begin, const void* end, Buffer* out) {
  auto b_addr = reinterpret_cast<uintptr_t>(begin);
  auto e_addr = reinterpret_cast<uintptr_t>(end);
  if (!begin || !end || end <= begin) {
    FX_LOGS(FATAL) << "Invalid region: begin=0x" << std::hex << b_addr << ", end=0x" << e_addr;
  }
  Create(e_addr - b_addr, out);
  source_ = begin;
  Update();
}

void SharedMemory::Link(Buffer buf, bool inline_size) {
  Reset();
  vmo_ = std::move(buf.vmo);
  capacity_ = buf.size;
  size_t aligned_capacity = fbl::round_up(capacity_, ZX_PAGE_SIZE);
  zx_status_t status = zx::vmar::root_self()->map(kOptions, 0, vmo_, 0, aligned_capacity, &addr_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to map VMO: " << zx_status_get_string(status);
  }
  if (!inline_size) {
    size_ = buf.size;
    return;
  }
  auto* header = reinterpret_cast<InlineHeader*>(addr_);
  capacity_ -= sizeof(InlineHeader);
  if (strncmp(header->magic, kInlineMagic, sizeof(header->magic)) != 0) {
    FX_LOGS(FATAL) << "Bad inline header: size=" << header->size;
  }
  size_ = kInlinedSize;
}

zx_status_t SharedMemory::Write(const void* src, size_t len) {
  if (addr_ == 0) {
    return ZX_ERR_BAD_STATE;
  }
  size_t offset = size();
  if (offset == capacity_) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  size_t left = capacity_ - offset;
  auto* dst = data() + offset;
  if (left < len) {
    memcpy(dst, src, left);
    SetSize(capacity_);
    return ZX_ERR_BUFFER_TOO_SMALL;
  } else {
    memcpy(dst, src, len);
    SetSize(offset + len);
    return ZX_OK;
  }
}

void SharedMemory::Update() {
  if (source_) {
    memcpy(begin(), source_, capacity_);
    size_ = capacity_;
  }
}

void SharedMemory::Clear() { SetSize(0); }

void SharedMemory::Reset() {
  if (is_mapped()) {
    zx::vmar::root_self()->unmap(addr_, AlignedCapacity(capacity_, size_));
  }
  vmo_.reset();
  addr_ = 0;
  capacity_ = 0;
  source_ = nullptr;
  size_ = 0;
}

// Private methods

void* SharedMemory::Begin() const {
  if (size_ != kInlinedSize) {
    return reinterpret_cast<void*>(addr_);
  } else {
    return reinterpret_cast<void*>(addr_ + sizeof(InlineHeader));
  }
}

void* SharedMemory::End() const {
  if (size_ != kInlinedSize) {
    return reinterpret_cast<void*>(addr_ + capacity_);
  } else {
    return reinterpret_cast<void*>(addr_ + sizeof(InlineHeader) + capacity_);
  }
}

size_t SharedMemory::GetSize() const {
  if (size_ != kInlinedSize) {
    return size_;
  }
  auto* header = reinterpret_cast<InlineHeader*>(addr_);
  return header->size;
}

void SharedMemory::SetSize(size_t size) {
  FX_CHECK(size <= capacity_);
  if (size_ != kInlinedSize) {
    size_ = size;
    return;
  }
  auto* header = reinterpret_cast<InlineHeader*>(addr_);
  header->size = size;
}

}  // namespace fuzzing
