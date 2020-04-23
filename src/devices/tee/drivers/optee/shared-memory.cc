// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <memory>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

namespace optee {

std::optional<SharedMemoryView> SharedMemoryRangeTraits::SliceByVaddr(zx_vaddr_t start,
                                                                      zx_vaddr_t end) const {
  if (end <= start || !ContainsVaddr(start) || !ContainsVaddr(end - 1)) {
    return std::nullopt;
  }

  zx_off_t offset = start - vaddr();
  return std::make_optional(SharedMemoryView(start, paddr() + offset, end - start));
}

std::optional<SharedMemoryView> SharedMemoryRangeTraits::SliceByPaddr(zx_paddr_t start,
                                                                      zx_paddr_t end) const {
  if (end <= start || !ContainsPaddr(start) || !ContainsPaddr(end - 1)) {
    return std::nullopt;
  }

  zx_off_t offset = start - paddr();
  return std::make_optional(SharedMemoryView(vaddr() + offset, start, end - start));
}

zx_status_t SharedMemoryManager::Create(zx_paddr_t shared_mem_start, size_t shared_mem_size,
                                        ddk::MmioBuffer secure_world_memory, zx::bti bti,
                                        std::unique_ptr<SharedMemoryManager>* out_manager) {
  ZX_DEBUG_ASSERT(out_manager != nullptr);

  // Round the start and end to the nearest page boundaries within the range and calculate a
  // new size.
  shared_mem_start = fbl::round_up(shared_mem_start, static_cast<uint32_t>(PAGE_SIZE));
  const zx_paddr_t shared_mem_end =
      fbl::round_down(shared_mem_start + shared_mem_size, static_cast<uint32_t>(PAGE_SIZE));
  if (shared_mem_end <= shared_mem_start) {
    zxlogf(ERROR, "optee: no shared memory available from secure world");
    return ZX_ERR_NO_RESOURCES;
  }
  shared_mem_size = shared_mem_end - shared_mem_start;

  std::optional<ddk::MmioPinnedBuffer> pinned;
  zx_status_t status = secure_world_memory.Pin(bti, &pinned);
  if (status != ZX_OK) {
    zxlogf(ERROR, "optee: unable to pin secure world memory");
    return status;
  }

  // The secure world shared memory exists within some subrange of the secure_world_memory.
  // Get the addresses from the io_buffer and validate that the requested subrange is within
  // the mmio range.
  const zx_vaddr_t secure_world_vaddr = reinterpret_cast<zx_vaddr_t>(secure_world_memory.get());
  const zx_paddr_t secure_world_paddr = pinned->get_paddr();
  const size_t secure_world_size = secure_world_memory.get_size();

  if ((shared_mem_start < secure_world_paddr) ||
      (shared_mem_end > secure_world_paddr + secure_world_size)) {
    zxlogf(ERROR, "optee: shared memory not within secure os memory");
    return ZX_ERR_INTERNAL;
  }

  if (shared_mem_size < 2 * kDriverPoolSize) {
    zxlogf(ERROR, "optee: shared memory is not large enough");
    return ZX_ERR_NO_RESOURCES;
  }

  const zx_off_t shared_mem_offset = shared_mem_start - secure_world_paddr;

  std::unique_ptr<SharedMemoryManager> manager(new SharedMemoryManager(
      secure_world_vaddr + shared_mem_offset, secure_world_paddr + shared_mem_offset,
      shared_mem_size, std::move(secure_world_memory), *std::move(pinned)));

  *out_manager = std::move(manager);
  return ZX_OK;
}

SharedMemoryManager::SharedMemoryManager(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr,
                                         size_t total_size, ddk::MmioBuffer secure_world_memory,
                                         ddk::MmioPinnedBuffer secure_world_memory_pin)
    : secure_world_memory_(std::move(secure_world_memory)),
      secure_world_memory_pin_(std::move(secure_world_memory_pin)),
      driver_pool_(base_vaddr, base_paddr, kDriverPoolSize),
      client_pool_(base_vaddr + kDriverPoolSize, base_paddr + kDriverPoolSize,
                   total_size - kDriverPoolSize) {}

}  // namespace optee
