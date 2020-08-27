// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <lib/zx/vmar.h>
#include <zircon/errors.h>

namespace fuzzing {

// These are the flags that the shared memory should be mapped with.
static const zx_vm_option_t kSharedVmOptions =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE | ZX_VM_REQUIRE_NON_RESIZABLE;

SharedMemory::SharedMemory() : addr_(0), len_(0) {}

SharedMemory::SharedMemory(SharedMemory &&other)
    : vmo_(std::move(other.vmo_)), addr_(other.addr_), len_(other.len_) {
  other.addr_ = 0;
  other.len_ = 0;
}

SharedMemory::~SharedMemory() { Reset(); }

zx_status_t SharedMemory::Create(size_t len) {
  if (len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(len, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  return Link(vmo, len);
}

zx_status_t SharedMemory::Share(zx::vmo *out) {
  if (!out) {
    return ZX_ERR_INVALID_ARGS;
  }
  return vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, out);
}

zx_status_t SharedMemory::Link(const zx::vmo &vmo, size_t len) {
  if (len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  Reset();
  size_t size;
  zx_status_t status = vmo.get_size(&size);
  if (status != ZX_OK) {
    return status;
  }
  if (size < len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  zx::vmo tmp;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &tmp);
  if (status != ZX_OK) {
    return status;
  }
  zx_vaddr_t addr;
  status = zx::vmar::root_self()->map(kSharedVmOptions, 0, tmp, 0, size, &addr);
  if (status != ZX_OK) {
    return status;
  }
  vmo_ = std::move(tmp);
  addr_ = addr;
  len_ = len;
  return ZX_OK;
}

void SharedMemory::Reset() {
  if (addr_ != 0) {
    zx::vmar::root_self()->unmap(addr_, len_);
    vmo_.reset();
    addr_ = 0;
    len_ = 0;
  }
}

}  // namespace fuzzing
