// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_bus_mapper.h"
#include "platform_trace.h"
#include <ddk/driver.h>

namespace magma {

ZirconPlatformBusMapper::BusMapping::~BusMapping()
{
    auto bti = bus_transaction_initiator_.lock();
    if (bti) {
        zx_status_t status = zx_bti_unpin(bti->get(), page_addr_[0]);
        if (status != ZX_OK) {
            printf("zx_bti_unpin failed: %d\n", status);
        }
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

    auto mapping =
        std::make_unique<BusMapping>(start_page_index, page_count, bus_transaction_initiator_);

    zx_status_t status;
    {
        TRACE_DURATION("magma", "bti pin");
        status = zx_bti_pin(bus_transaction_initiator_->get(),
                            ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_PERM_EXECUTE,
                            static_cast<ZirconPlatformBuffer*>(buffer)->handle(),
                            start_page_index * PAGE_SIZE, page_count * PAGE_SIZE,
                            mapping->Get().data(), page_count);
    }
    if (status != ZX_OK)
        return DRETP(nullptr, "zx_bti_pin failed: %d", status);

    return mapping;
}

std::unique_ptr<PlatformBusMapper>
PlatformBusMapper::Create(std::shared_ptr<PlatformHandle> bus_transaction_initiator)
{
    return std::make_unique<ZirconPlatformBusMapper>(
        std::static_pointer_cast<ZirconPlatformHandle>(bus_transaction_initiator));
}

} // namespace magma
