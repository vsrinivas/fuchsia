// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "../allocation.h"
#include "fake_pciroot.h"
#include <ddktl/protocol/pciroot.h>
#include <fbl/unique_ptr.h>
#include <zircon/limits.h>
#include <zxtest/zxtest.h>

namespace pci {

// Tests that GetAddressSpace / FreeAddressSpace are equally alled when
// allocations using PcirootProtocol are created and freed through
// PciRootAllocation and PciRegionAllocation dtors.
TEST(PciAllocationTest, BalancedAllocation) {
    std::unique_ptr<FakePciroot> pciroot;
    ASSERT_EQ(ZX_OK, FakePciroot::Create(0, 0, &pciroot));
    ddk::PcirootProtocolClient client(pciroot->proto());
    PciRootAllocator root(client, PCI_ADDRESS_SPACE_MMIO, false);
    PciAllocator* root_ptr = &root;

    {
        fbl::unique_ptr<PciAllocation> alloc;
        EXPECT_EQ(ZX_OK, root_ptr->GetRegion(ZX_PAGE_SIZE, &alloc));
        EXPECT_EQ(1, pciroot->allocation_cnt());
        PciRegionAllocator region;
        region.AddAddressSpace(std::move(alloc));
    }

    EXPECT_EQ(0, pciroot->allocation_cnt());
}

// Since text allocations lack a valid resource they should fail when
// CreateVMObject is called
TEST(PciAllocationTest, VmoCreationFailure) {
    std::unique_ptr<FakePciroot> pciroot;
    ASSERT_EQ(ZX_OK, FakePciroot::Create(0, 0, &pciroot));
    ddk::PcirootProtocolClient client(pciroot->proto());

    fbl::unique_ptr<zx::vmo> vmo;
    PciRootAllocator root(client, PCI_ADDRESS_SPACE_MMIO, false);
    PciAllocator* root_ptr = &root;
    fbl::unique_ptr<PciAllocation> alloc;
    EXPECT_EQ(ZX_OK, root_ptr->GetRegion(ZX_PAGE_SIZE, &alloc));
    EXPECT_NE(ZX_OK, alloc->CreateVmObject(&vmo));
}

} // namespace pci
