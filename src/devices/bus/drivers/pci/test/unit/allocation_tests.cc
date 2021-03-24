// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/zx/status.h>
#include <zircon/limits.h>

#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/allocation.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"

namespace pci {

FakePciroot* RetrieveFakeFromClient(const ddk::PcirootProtocolClient& client) {
  pciroot_protocol_t proto;
  client.GetProto(&proto);
  return static_cast<FakePciroot*>(proto.ctx);
}

// Tests that GetAddressSpace / FreeAddressSpace are equally called when
// allocations using PcirootProtocol are created and freed through
// PciRootAllocation and PciRegionAllocation dtors.
TEST(PciAllocationTest, BalancedAllocation) {
  FakePciroot pciroot;
  ddk::PcirootProtocolClient client(pciroot.proto());
  FakePciroot* fake_impl = RetrieveFakeFromClient(client);
  PciRootAllocator root_alloc(client, PCI_ADDRESS_SPACE_MEMORY, false);
  {
    auto alloc1 = root_alloc.Allocate(std::nullopt, ZX_PAGE_SIZE);
    EXPECT_TRUE(alloc1.is_ok());
    EXPECT_EQ(1, fake_impl->allocation_eps().size());
    auto alloc2 = root_alloc.Allocate(1024, ZX_PAGE_SIZE);
    EXPECT_TRUE(alloc2.is_ok());
    EXPECT_EQ(2, fake_impl->allocation_eps().size());
  }

  // TODO(fxbug.dev/32978): Rework this with the new eventpair model of GetAddressSpace
  // EXPECT_EQ(0, fake_impl->allocation_cnt());
}

// Since test allocations lack a valid resource they should fail when
// CreateVMObject is called
TEST(PciAllocationTest, VmoCreationFailure) {
  FakePciroot pciroot;
  ddk::PcirootProtocolClient client(pciroot.proto());

  zx::vmo vmo;
  PciRootAllocator root(client, PCI_ADDRESS_SPACE_MEMORY, false);
  PciAllocator* root_ptr = &root;
  auto alloc = root_ptr->Allocate(std::nullopt, ZX_PAGE_SIZE);
  EXPECT_TRUE(alloc.is_ok());
  EXPECT_OK(alloc->CreateVmObject(&vmo));
}

}  // namespace pci
