// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/limits.h>

#include <ddktl/protocol/pciroot.h>
#include <zxtest/zxtest.h>

#include "../../allocation.h"
#include "../fakes/fake_pciroot.h"

namespace pci {

FakePciroot* RetrieveFakeFromClient(const ddk::PcirootProtocolClient& client) {
  pciroot_protocol_t proto;
  client.GetProto(&proto);
  return static_cast<FakePciroot*>(proto.ctx);
}

// Tests that GetAddressSpace / FreeAddressSpace are equally alled when
// allocations using PcirootProtocol are created and freed through
// PciRootAllocation and PciRegionAllocation dtors.
TEST(PciAllocationTest, BalancedAllocation) {
  std::unique_ptr<FakePciroot> pciroot;
  ASSERT_OK(FakePciroot::Create(0, 0, &pciroot));
  ddk::PcirootProtocolClient client(pciroot->proto());
  FakePciroot* fake_impl = RetrieveFakeFromClient(client);
  PciRootAllocator root_alloc(client, PCI_ADDRESS_SPACE_MMIO, false);
  {
    std::unique_ptr<PciAllocation> alloc1, alloc2;
    EXPECT_OK(root_alloc.PciAllocator::AllocateWindow(ZX_PAGE_SIZE, &alloc1));
    EXPECT_EQ(1, fake_impl->allocation_cnt());
    EXPECT_OK(root_alloc.PciAllocator::AllocateWindow(ZX_PAGE_SIZE, &alloc2));
    EXPECT_EQ(2, fake_impl->allocation_cnt());
  }

  EXPECT_EQ(0, fake_impl->allocation_cnt());
}

// Since text allocations lack a valid resource they should fail when
// CreateVMObject is called
TEST(PciAllocationTest, VmoCreationFailure) {
  std::unique_ptr<FakePciroot> pciroot;
  ASSERT_OK(FakePciroot::Create(0, 0, &pciroot));
  ddk::PcirootProtocolClient client(pciroot->proto());

  zx::vmo vmo;
  PciRootAllocator root(client, PCI_ADDRESS_SPACE_MMIO, false);
  PciAllocator* root_ptr = &root;
  std::unique_ptr<PciAllocation> alloc;
  EXPECT_OK(root_ptr->AllocateWindow(ZX_PAGE_SIZE, &alloc));
  EXPECT_NE(ZX_OK, alloc->CreateVmObject(&vmo));
}

}  // namespace pci
