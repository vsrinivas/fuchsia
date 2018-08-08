// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_bus_mapper.h"
#include "platform_trace.h"
#include <ddk/driver.h>
#include <lib/zx/process.h>

namespace magma {

ZirconPlatformBusMapper::BusMapping::~BusMapping()
{
    zx_status_t status = pmt_.unpin();
    if (status != ZX_OK) {
        DLOG("zx_pmt_unpin failed: %d\n", status);
    }
}

std::unique_ptr<PlatformBusMapper::BusMapping>
ZirconPlatformBusMapper::MapPageRangeBus(magma::PlatformBuffer* buffer, uint32_t start_page_index,
                                         uint32_t page_count)
{
    TRACE_DURATION("magma", "MapPageRangeBus");
    static_assert(sizeof(zx_paddr_t) == sizeof(uint64_t), "unexpected sizeof(zx_paddr_t)");

    if ((page_count == 0) || (start_page_index + page_count) * PAGE_SIZE > buffer->size())
        return DRETP(nullptr, "Invalid range: %d, %d\n", start_page_index, page_count);

    std::vector<uint64_t> page_addr(page_count);
    zx::pmt pmt;

    zx_status_t status;
    {
        TRACE_DURATION("magma", "bti pin");
        status = zx_bti_pin(bus_transaction_initiator_->get(),
                            ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_PERM_EXECUTE,
                            static_cast<ZirconPlatformBuffer*>(buffer)->handle(),
                            start_page_index * PAGE_SIZE, page_count * PAGE_SIZE, page_addr.data(),
                            page_count, pmt.reset_and_get_address());
    }
    if (status != ZX_OK) {
        zx_info_kmem_stats_t kmem_stats;
        zx_object_get_info(get_root_resource(), ZX_INFO_KMEM_STATS, &kmem_stats, sizeof(kmem_stats),
                           nullptr, nullptr);
        zx_info_task_stats_t task_stats = {};
        zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats, sizeof(task_stats), nullptr,
                                      nullptr);
        magma::log(magma::LOG_WARNING,
                   "Failed to pin 0x%x pages (0x%lx bytes) with status %d. Out of Memory?\n"
                   "mem_mapped_bytes: 0x%lx mem_private_bytes: 0x%lx mem_shared_bytes: 0x%lx\n"
                   "total_bytes: 0x%lx free_bytes 0x%lx: wired_bytes: 0x%lx vmo_bytes: 0x%lx\n"
                   "mmu_overhead_bytes: 0x%lx other_bytes: 0x%lx\n",
                   page_count, static_cast<uint64_t>(page_count) * PAGE_SIZE, status,
                   task_stats.mem_mapped_bytes, task_stats.mem_private_bytes,
                   task_stats.mem_shared_bytes, kmem_stats.total_bytes, kmem_stats.free_bytes,
                   kmem_stats.wired_bytes, kmem_stats.vmo_bytes, kmem_stats.mmu_overhead_bytes,
                   kmem_stats.other_bytes);
        return nullptr;
    }

    auto mapping =
        std::make_unique<BusMapping>(start_page_index, std::move(page_addr), std::move(pmt));

    return mapping;
}

std::unique_ptr<PlatformBusMapper>
PlatformBusMapper::Create(std::shared_ptr<PlatformHandle> bus_transaction_initiator)
{
    return std::make_unique<ZirconPlatformBusMapper>(
        std::static_pointer_cast<ZirconPlatformHandle>(bus_transaction_initiator));
}

} // namespace magma
