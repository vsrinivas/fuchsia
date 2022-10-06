// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_
#define SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_

#include <magma/magma.h>

#if defined(__Fuchsia__)
#include <lib/zx/vmar.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace magma {

bool MapCpuHelper(magma_buffer_t buffer, size_t offset, size_t length, void** addr_out) {
  magma_handle_t handle;
  magma_status_t status = magma_get_buffer_handle2(buffer, &handle);
  if (status != MAGMA_STATUS_OK)
    return false;

#if defined(__Fuchsia__)
  zx::vmo vmo(handle);

  zx_vaddr_t zx_vaddr;
  zx_status_t zx_status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                                     0,  // vmar_offset,
                                                     vmo, offset, length, &zx_vaddr);
  if (zx_status != ZX_OK)
    return false;

  *addr_out = reinterpret_cast<void*>(zx_vaddr);

  return true;

#elif defined(__linux__)
  int fd = handle;

  void* addr = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

  close(fd);

  if (addr == MAP_FAILED)
    return false;

  *addr_out = addr;

  return true;
#else
#error Unsupported
#endif
}

bool UnmapCpuHelper(void* addr, size_t length) {
#if defined(__Fuchsia__)
  zx_status_t status = zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(addr), length);
  return status == ZX_OK;
#elif defined(__linux__)
  int ret = munmap(addr, length);
  return ret == 0;
#else
#error Unsupported
#endif
}

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_
