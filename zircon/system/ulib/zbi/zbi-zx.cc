// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbi/zbi-zx.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <string.h>
#include <zircon/assert.h>

#include <utility>

namespace zbi {

namespace {

size_t PageRound(size_t size) { return (size + PAGE_SIZE) & -(size_t)PAGE_SIZE; }

}  // namespace

zx_status_t ZbiVMO::Init(zx::vmo vmo) {
  vmo_ = std::move(vmo);
  auto status = vmo_.get_size(&capacity_);
  if (status == ZX_OK && capacity_ > 0) {
    status = Map();
  }
  return status;
}

zx::vmo ZbiVMO::Release() {
  Unmap();
  capacity_ = 0;
  return std::move(vmo_);
}

ZbiVMO::~ZbiVMO() { Unmap(); }

zx_status_t ZbiVMO::Map() {
  uintptr_t mapping;
  auto status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_, 0,
                                           capacity_, &mapping);
  if (status == ZX_OK) {
    base_ = reinterpret_cast<uint8_t*>(mapping);
  }
  return status;
}

void ZbiVMO::Unmap() {
  if (base_) {
    [[maybe_unused]] auto status =
        zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(base_), capacity_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    base_ = nullptr;
  }
}

zbi_result_t ZbiVMO::AppendSection(uint32_t length, uint32_t type, uint32_t extra, uint32_t flags,
                                   const void* payload) {
  void* dest;
  auto result = CreateSection(length, type, extra, flags, &dest);
  if (result == ZBI_RESULT_OK) {
    memcpy(dest, payload, length);
  }
  return result;
}

zbi_result_t ZbiVMO::CreateSection(uint32_t length, uint32_t type, uint32_t extra, uint32_t flags,
                                   void** payload) {
  auto result = CreateEntry(type, extra, flags, length, payload);
  if (result == ZBI_RESULT_TOO_BIG) {
    const size_t new_capacity = PageRound(Length() + sizeof(zbi_header_t) + length);
    ZX_DEBUG_ASSERT(new_capacity > capacity_);
    auto status = vmo_.set_size(new_capacity);
    if (status == ZX_OK) {
      Unmap();
      capacity_ = new_capacity;
      status = Map();
    }
    if (status == ZX_OK) {
      result = CreateEntry(type, extra, flags, length, payload);
    }
  }
  return result;
}

zbi_result_t ZbiVMO::SplitComplete(ZbiVMO* kernel, ZbiVMO* data) const {
  // First check that it's a proper complete ZBI.  After this it should be
  // safe to trust the headers (modulo racing modification of the original
  // VMO, which we can't help).
  auto result = CheckComplete();
  if (result != ZBI_RESULT_OK) {
    return result;
  }

  // First clone a VMO covering just the leading kernel portion of the ZBI.
  auto kernel_hdr = Header() + 1;
  const uint32_t kernel_size = static_cast<uint32_t>(sizeof(zbi_header_t) * 2) + kernel_hdr->length;
  const size_t kernel_vmo_size = PageRound(kernel_size);
  auto status = vmo_.create_child(ZX_VMO_CHILD_COPY_ON_WRITE | ZX_VMO_CHILD_RESIZABLE, 0,
                                  kernel_vmo_size, &kernel->vmo_);
  if (status != ZX_OK) {
    return ZBI_RESULT_TOO_BIG;
  }

  // Map it in.
  kernel->Unmap();  // Just in case.
  kernel->capacity_ = kernel_vmo_size;
  status = kernel->Map();
  if (status != ZX_OK) {
    return ZBI_RESULT_TOO_BIG;
  }
  // Update the size in the copied container header.
  kernel->Header()->length = kernel_size - static_cast<uint32_t>(sizeof(zbi_header_t));

  // Now create a VMO for the remainder.
  const uint32_t data_payload_size = Length() - kernel_size;
  const size_t data_vmo_size =
      PageRound(data_payload_size + static_cast<uint32_t>(sizeof(zbi_header_t)));

  status = vmo_.create(data_vmo_size, ZX_VMO_RESIZABLE, &data->vmo_);
  if (status != ZX_OK) {
    return ZBI_RESULT_TOO_BIG;
  }

  // Map it in.
  data->Unmap();  // Just in case.
  data->capacity_ = data_vmo_size;
  status = data->Map();
  if (status != ZX_OK) {
    return ZBI_RESULT_TOO_BIG;
  }

  // Fill in the header and data (if not already virtually copied).
  *data->Header() = (zbi_header_t)ZBI_CONTAINER_HEADER(data_payload_size);
  memcpy(data->Payload(), Base() + kernel_size, data_payload_size);

  return ZBI_RESULT_OK;
}

}  // namespace zbi
