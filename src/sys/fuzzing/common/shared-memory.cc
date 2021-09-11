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
namespace {

// These are the flags that the shared memory should be mapped with.
const zx_vm_option_t kOptions =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE | ZX_VM_REQUIRE_NON_RESIZABLE;

// If size is |kInlinedSize|, buffer starts with an inline header.
const char* kInlineMagic = "INLINED";

}  // namespace

// Helper function to suppress linter warning.
template <typename T>
void Cast(zx_vaddr_t addr, T** out) {
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  *out = reinterpret_cast<T*>(addr);
}

// Public methods

SharedMemory::~SharedMemory() { Reset(); }

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
  vmo_ = std::move(other.vmo_);
  mapped_addr_ = other.mapped_addr_;
  mapped_size_ = other.mapped_size_;
  data_ = other.data_;
  size_ = other.size_;
  header_ = other.header_;
  mirror_ = other.mirror_;
  poisoning_ = other.poisoning_;
  unpoisoned_size_ = other.unpoisoned_size_;
  other.mapped_addr_ = 0;
  other.poisoning_ = false;
  other.Reset();
  return *this;
}

size_t SharedMemory::size() {
  if (header_ && header_->size != size_) {
    size_ = header_->size;
    PoisonAfter(size_);
  }
  return size_;
}

void SharedMemory::Reserve(size_t capacity) {
  Create(sizeof(InlineHeader) + capacity);
  Map();
  Cast(mapped_addr_, &header_);
  memcpy(header_->magic, kInlineMagic, sizeof(header_->magic));
  header_->size = 0;
  Cast(mapped_addr_ + sizeof(InlineHeader), &data_);
  size_ = 0;
  unpoisoned_size_ = this->capacity();
}

void SharedMemory::Mirror(void* data, size_t size) {
  FX_CHECK(data && size);
  Create(size);
  Map();
  mirror_ = data;
  Cast(mapped_addr_, &data_);
  size_ = size;
  unpoisoned_size_ = capacity();
  Update();
}

Buffer SharedMemory::Share() {
  Buffer buf;
  buf.size = header_ ? mapped_size_ : size_;
  auto status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &buf.vmo);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to duplicate VMO: " << zx_status_get_string(status);
  }
  return buf;
}

void SharedMemory::LinkReserved(Buffer&& buf) {
  Reset();
  Map(std::move(buf));
  Cast(mapped_addr_, &header_);
  if (strncmp(header_->magic, kInlineMagic, sizeof(header_->magic)) != 0) {
    FX_LOGS(FATAL) << "Bad inline header: magic=0x" << std::hex << header_->magic << std::dec;
  }
  Cast(mapped_addr_ + sizeof(InlineHeader), &data_);
  unpoisoned_size_ = capacity();
}

void SharedMemory::LinkMirrored(Buffer&& buf) {
  Reset();
  size_ = buf.size;
  Map(std::move(buf));
  Cast(mapped_addr_, &data_);
  unpoisoned_size_ = capacity();
}

void SharedMemory::SetPoisoning(bool enable) {
  if (enable != poisoning_) {
    PoisonAfter(enable ? size_ : capacity());
    poisoning_ = enable;
  }
}

void SharedMemory::Resize(size_t size) {
  FX_DCHECK(header_);
  FX_DCHECK(size <= capacity());
  size_ = size;
  header_->size = size_;
}

void SharedMemory::Write(uint8_t one_byte) {
  FX_DCHECK(header_);
  FX_DCHECK(sizeof(*header_) + size_ < mapped_size_);
  FX_DCHECK(!poisoning_);
  data_[size_++] = one_byte;
  header_->size = size_;
}

void SharedMemory::Write(const void* data, size_t size) {
  if (!size) {
    return;
  }
  FX_DCHECK(header_);
  FX_DCHECK(sizeof(*header_) + size_ + size <= mapped_size_);
  FX_DCHECK(!poisoning_);
  memcpy(data_ + size_, data, size);
  size_ += size;
  header_->size = size_;
}

void SharedMemory::Update() {
  FX_DCHECK(mirror_);
  memcpy(data_, mirror_, size_);
}

void SharedMemory::Clear() {
  if (header_) {
    SetPoisoning(false);
    Resize(0);
  } else {
    memset(mirror_, 0, size_);
  }
}

// Private methods

void SharedMemory::Reset() {
  SetPoisoning(false);
  if (is_mapped()) {
    zx::vmar::root_self()->unmap(mapped_addr_, mapped_size_);
  }
  vmo_.reset();
  mapped_addr_ = 0;
  mapped_size_ = 0;
  data_ = nullptr;
  size_ = 0;
  header_ = nullptr;
  mirror_ = nullptr;
  unpoisoned_size_ = 0;
}

void SharedMemory::Create(size_t capacity) {
  Reset();
  mapped_size_ = fbl::round_up(capacity, ZX_PAGE_SIZE);
  auto status = zx::vmo::create(mapped_size_, 0, &vmo_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create VMO: " << zx_status_get_string(status);
  }
}

void SharedMemory::Map(Buffer&& buf) {
  vmo_ = std::move(buf.vmo);
  mapped_size_ = fbl::round_up(buf.size, ZX_PAGE_SIZE);
  Map();
}

void SharedMemory::Map() {
  FX_DCHECK(!mapped_addr_);
  auto status = zx::vmar::root_self()->map(kOptions, 0, vmo_, 0, mapped_size_, &mapped_addr_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to map VMO: " << zx_status_get_string(status);
  }
}

void SharedMemory::PoisonAfter(size_t size) {
  auto max_size = capacity();
  if (unpoisoned_size_ == size) {
    return;
  }
  if (unpoisoned_size_ < max_size) {
    ASAN_UNPOISON_MEMORY_REGION(data_ + unpoisoned_size_, max_size - unpoisoned_size_);
  }
  if (size < max_size) {
    ASAN_POISON_MEMORY_REGION(data_ + size, max_size - size);
  }
  unpoisoned_size_ = size;
}

}  // namespace fuzzing
