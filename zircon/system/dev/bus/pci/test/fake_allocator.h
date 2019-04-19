// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_ALLOCATOR_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_ALLOCATOR_H_

#include "../allocation.h"

namespace pci {

class FakeAllocator : public PciAllocator {
public:
    zx_status_t GetRegion(zx_paddr_t base, size_t size,
                          fbl::unique_ptr<PciAllocation>* out_alloc) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
};

} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_ALLOCATOR_H_
