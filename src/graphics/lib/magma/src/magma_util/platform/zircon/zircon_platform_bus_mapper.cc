// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_bus_mapper.h"

#include <lib/ddk/driver.h>
#include <lib/zx/process.h>

#include "platform_logger.h"
#include "platform_trace.h"

namespace magma {

ZirconPlatformBusMapper::BusMapping::~BusMapping() {
  for (auto& pmt : pmt_) {
    zx_status_t status = pmt.unpin();
    if (status != ZX_OK) {
      DLOG("zx_pmt_unpin failed: %d\n", status);
    }
  }
}

std::unique_ptr<PlatformBusMapper::BusMapping> ZirconPlatformBusMapper::MapPageRangeBus(
    magma::PlatformBuffer* buffer, uint64_t start_page_index, uint64_t page_count) {
  TRACE_DURATION("magma", "MapPageRangeBus");
  static_assert(sizeof(zx_paddr_t) == sizeof(uint64_t), "unexpected sizeof(zx_paddr_t)");

  if ((page_count == 0) || (start_page_index + page_count) * magma::page_size() > buffer->size())
    return DRETP(nullptr, "Invalid range: %lu, %lu", start_page_index, page_count);

  // Pin in 256MB chunks because Zircon can't pin a 512MB buffer (fxbug.dev/45016)
  const uint64_t kMaxPageCount = 256 * 1024 * 1024 / magma::page_size();
  uint64_t pmt_count = magma::round_up(page_count, kMaxPageCount) / kMaxPageCount;

  std::vector<uint64_t> page_addr(page_count);
  std::vector<zx::pmt> pmt(pmt_count);

  for (uint32_t i = 0; i < pmt_count; i++) {
    uint64_t chunk_page_count = page_count - (i * kMaxPageCount);

    if (chunk_page_count > kMaxPageCount) {
      chunk_page_count = kMaxPageCount;
    }

    uint64_t size = chunk_page_count * magma::page_size();
    uint64_t page_offset = i * kMaxPageCount;

    zx_status_t status;
    {
      TRACE_DURATION("magma", "bti pin", "size", size);
      status = zx_bti_pin(bus_transaction_initiator_->get(),
                          ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_PERM_EXECUTE,
                          static_cast<ZirconPlatformBuffer*>(buffer)->handle(),
                          (start_page_index + page_offset) * magma::page_size(), size,
                          page_addr.data() + page_offset, chunk_page_count,
                          pmt[i].reset_and_get_address());
    }
    if (status != ZX_OK) {
      zx_info_kmem_stats_t kmem_stats;
      zx_object_get_info(get_root_resource(), ZX_INFO_KMEM_STATS, &kmem_stats, sizeof(kmem_stats),
                         nullptr, nullptr);
      zx_info_task_stats_t task_stats = {};
      zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats, sizeof(task_stats), nullptr,
                                    nullptr);
      MAGMA_LOG(WARNING,
                "Failed to pin pmt %u 0x%lx pages (0x%lx bytes) with status %d. Out of Memory?\n"
                "mem_mapped_bytes: 0x%lx mem_private_bytes: 0x%lx mem_shared_bytes: 0x%lx\n"
                "total_bytes: 0x%lx free_bytes 0x%lx: wired_bytes: 0x%lx vmo_bytes: 0x%lx\n"
                "mmu_overhead_bytes: 0x%lx other_bytes: 0x%lx\n",
                i, chunk_page_count, size, status, task_stats.mem_mapped_bytes,
                task_stats.mem_private_bytes, task_stats.mem_shared_bytes, kmem_stats.total_bytes,
                kmem_stats.free_bytes, kmem_stats.wired_bytes, kmem_stats.vmo_bytes,
                kmem_stats.mmu_overhead_bytes, kmem_stats.other_bytes);
      return nullptr;
    }
  }

  auto mapping =
      std::make_unique<BusMapping>(start_page_index, std::move(page_addr), std::move(pmt));

  return mapping;
}

std::unique_ptr<PlatformBuffer> ZirconPlatformBusMapper::CreateContiguousBuffer(
    size_t size, uint32_t alignment_log2, const char* name) {
  zx::vmo vmo;
  zx_status_t status = zx_vmo_create_contiguous(bus_transaction_initiator_->get(), size,
                                                alignment_log2, vmo.reset_and_get_address());
  if (status != ZX_OK)
    DRETP(nullptr, "Failed to create contiguous vmo: %d", status);
  vmo.set_property(ZX_PROP_NAME, name, strlen(name));
  return PlatformBuffer::Import(vmo.release());
}

std::unique_ptr<PlatformBusMapper> PlatformBusMapper::Create(
    std::shared_ptr<PlatformHandle> bus_transaction_initiator) {
  return std::make_unique<ZirconPlatformBusMapper>(
      std::static_pointer_cast<ZirconPlatformHandle>(bus_transaction_initiator));
}

}  // namespace magma
