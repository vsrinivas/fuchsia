// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "../../allocation.h"

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
