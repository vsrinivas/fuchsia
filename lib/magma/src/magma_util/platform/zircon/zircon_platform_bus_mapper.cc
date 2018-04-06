// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_bus_mapper.h"
#include "platform_trace.h"
#include <ddk/driver.h>

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
        status = zx_bti_pin_new(bus_transaction_initiator_->get(),
                                ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_PERM_EXECUTE,
                                static_cast<ZirconPlatformBuffer*>(buffer)->handle(),
                                start_page_index * PAGE_SIZE, page_count * PAGE_SIZE,
                                page_addr.data(), page_count,
                                pmt.reset_and_get_address());
    }
    if (status != ZX_OK)
        return DRETP(nullptr, "zx_bti_pin failed: %d", status);

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
