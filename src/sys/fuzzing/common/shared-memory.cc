// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/shared-memory.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmar.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <limits>

#include <fbl/algorithm.h>
#include <sanitizer/asan_interface.h>

namespace fuzzing {

// Public methods

SharedMemory::~SharedMemory() { Reset(); }

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
  vmo_ = std::move(other.vmo_);
  mirror_ = other.mirror_;
  data_ = other.data_;
  size_ = other.size_;
  mapped_size_ = other.mapped_size_;
  other.mapped_size_ = 0;
  other.Reset();
  return *this;
}

zx_status_t SharedMemory::Reserve(size_t capacity) {
  if (auto status = Create(capacity); status != ZX_OK) {
    return status;
  }
  return Resize(0);
}

zx_status_t SharedMemory::Mirror(void* data, size_t size) {
  if (!data || !size) {
    FX_LOGS(ERROR) << "Cannot mirror empty buffer.";
    return ZX_ERR_INVALID_ARGS;
  }
  if (auto status = Create(size); status != ZX_OK) {
    return status;
  }
  mirror_ = data;
  if (auto status = Resize(size); status != ZX_OK) {
    return status;
  }
  Update();
  return ZX_OK;
}

zx_status_t SharedMemory::Share(zx::vmo* out) const {
  if (auto status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, out); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate VMO: " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

zx_status_t SharedMemory::Link(zx::vmo vmo) {
  Reset();
  vmo_ = std::move(vmo);
  if (auto status = Map(); status != ZX_OK) {
    return status;
  }
  if (auto status = Read(); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t SharedMemory::Read() {
  FX_DCHECK(!mirror_);
  size_t size;
  if (auto status = vmo_.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get size from VMO: " << zx_status_get_string(status);
    return status;
  }
  Resize(size);
  return ZX_OK;
}

zx_status_t SharedMemory::Write(const void* data, size_t size) {
  FX_DCHECK(!mirror_);
  if (mapped_size_ < size) {
    FX_LOGS(ERROR) << "Failed to write to VMO: need " << size << " bytes, have " << mapped_size_
                   << ".";
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (auto status = Resize(size); status != ZX_OK) {
    return status;
  }
  if (size) {
    memcpy(data_, data, size);
  }
  return ZX_OK;
}

void SharedMemory::Update() {
  FX_DCHECK(mirror_);
  memcpy(data_, mirror_, size_);
}

void SharedMemory::Clear() {
  FX_DCHECK(mirror_);
  memset(mirror_, 0, size_);
}

// Private methods

void SharedMemory::Reset() {
  vmo_.reset();
  if (mapped_size_) {
    Unpoison(mapped_size_);
    auto mapped_addr = reinterpret_cast<zx_vaddr_t>(data_);
    zx::vmar::root_self()->unmap(mapped_addr, mapped_size_);
  }
  mirror_ = nullptr;
  data_ = nullptr;
  size_ = 0;
  mapped_size_ = 0;
}

zx_status_t SharedMemory::Create(size_t capacity) {
  Reset();
  auto mapped_size = fbl::round_up(capacity, zx_system_get_page_size());
  if (auto status = zx::vmo::create(mapped_size, 0, &vmo_); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create VMO: " << zx_status_get_string(status);
    return status;
  }
  return Map();
}

zx_status_t SharedMemory::Map() {
  if (auto status = vmo_.get_size(&mapped_size_); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get size of VMO: " << zx_status_get_string(status);
    return status;
  }
  zx_vaddr_t mapped_addr;
  if (auto status = zx::vmar::root_self()->map(
          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE | ZX_VM_REQUIRE_NON_RESIZABLE, 0,
          vmo_, 0, mapped_size_, &mapped_addr);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map VMO: " << zx_status_get_string(status);
    return status;
  }
  data_ = reinterpret_cast<uint8_t*>(mapped_addr);
  size_ = mapped_size_;
  return ZX_OK;
}

zx_status_t SharedMemory::Resize(size_t size) {
  if (size == size_) {
    return ZX_OK;
  }
  Unpoison(size);
  size_ = size;
  if (auto status = vmo_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size_, sizeof(size_));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set size for VMO: " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

void SharedMemory::Unpoison(size_t size) {
  if (size_ < mapped_size_) {
    ASAN_UNPOISON_MEMORY_REGION(data_ + size_, mapped_size_ - size_);
  }
  if (size < mapped_size_) {
    ASAN_POISON_MEMORY_REGION(data_ + size, mapped_size_ - size);
  }
}

}  // namespace fuzzing
