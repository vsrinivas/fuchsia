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

class FakePcirootEx : public FakePciroot {
public:
    zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t len, pci_address_space_t type,
                                       bool low, uint64_t* out_base,
                                       zx::resource* resource) override {
        allocation_cnt_++;
        return ZX_OK;
    }
    virtual zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len,
                                                pci_address_space_t type) override {
        allocation_cnt_--;
        return ZX_OK;
    }

    int32_t allocation_cnt() { return allocation_cnt_; }

private:
    int32_t allocation_cnt_ = 0;
};

namespace pci {

// Tests that GetAddressSpace / FreeAddressSpace are equally alled when
// allocations using PcirootProtocol are created and freed through
// PciRootAllocation and PciRegionAllocation dtors.
TEST(PciAllocationTest, BalancedAllocation){
    FakePcirootEx fake_pciroot;
    ddk::PcirootProtocolClient client(fake_pciroot.proto());
    PciRootAllocator root(client, PCI_ADDRESS_SPACE_MMIO, false);
    PciAllocator* root_ptr = &root;

    {
        fbl::unique_ptr<PciAllocation> alloc;

        EXPECT_EQ(ZX_OK, root_ptr->GetRegion(ZX_PAGE_SIZE, &alloc));
        EXPECT_EQ(1, fake_pciroot.allocation_cnt());
        PciRegionAllocator region;
        region.AddAddressSpace(std::move(alloc));
    }

    EXPECT_EQ(0, fake_pciroot.allocation_cnt());
}

// Since text allocations lack a valid resource they should fail when
// CreateVMObject is called
TEST(PciAllocationTest, VmoCreationFailure){
    FakePcirootEx fake_pciroot;
    ddk::PcirootProtocolClient client(fake_pciroot.proto());

    fbl::unique_ptr<zx::vmo> vmo;
    PciRootAllocator root(client, PCI_ADDRESS_SPACE_MMIO, false);
    PciAllocator* root_ptr = &root;
    fbl::unique_ptr<PciAllocation> alloc;
    EXPECT_EQ(ZX_OK, root_ptr->GetRegion(ZX_PAGE_SIZE, &alloc));
    EXPECT_NE(ZX_OK, alloc->CreateVmObject(&vmo));
}

} // namespace pci
