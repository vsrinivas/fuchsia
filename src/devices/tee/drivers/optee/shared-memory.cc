// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <lib/ddk/debug.h>

#include <memory>

#include <fbl/algorithm.h>

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

zx_status_t SharedMemoryManager::Create(ddk::MmioBuffer shared_memory,
                                        zx_paddr_t shared_memory_paddr,
                                        std::unique_ptr<SharedMemoryManager>* out_manager) {
  ZX_DEBUG_ASSERT(out_manager != nullptr);

  if (shared_memory.get_size() < 2 * kDriverPoolSize) {
    zxlogf(ERROR, "optee: shared memory is not large enough");
    return ZX_ERR_NO_RESOURCES;
  }

  // Split the shared memory region into two pools: one for driver messages and one for client
  // application messages. The driver pool is a fixed size, and the client pool will get the
  // remainder of the shared memory region.
  zx_vaddr_t shared_memory_vaddr = reinterpret_cast<zx_vaddr_t>(shared_memory.get());
  PoolConfig driver_pool_config{
      .vaddr = shared_memory_vaddr, .paddr = shared_memory_paddr, .size = kDriverPoolSize};
  PoolConfig client_pool_config{.vaddr = shared_memory_vaddr + kDriverPoolSize,
                                .paddr = shared_memory_paddr + kDriverPoolSize,
                                .size = shared_memory.get_size() - kDriverPoolSize};

  std::unique_ptr<SharedMemoryManager> manager(
      new SharedMemoryManager(std::move(shared_memory), driver_pool_config, client_pool_config));

  *out_manager = std::move(manager);
  return ZX_OK;
}

SharedMemoryManager::SharedMemoryManager(ddk::MmioBuffer shared_memory,
                                         PoolConfig driver_pool_config,
                                         PoolConfig client_pool_config)
    : shared_memory_(std::move(shared_memory)),
      driver_pool_(driver_pool_config.vaddr, driver_pool_config.paddr, driver_pool_config.size),
      client_pool_(client_pool_config.vaddr, client_pool_config.paddr, client_pool_config.size) {}

}  // namespace optee
