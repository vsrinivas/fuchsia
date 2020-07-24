// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-resource/resource.h>
#include <lib/pci/pciroot.h>
#include <lib/pci/root_host.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/resource.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>

#include <memory>

#include <zxtest/zxtest.h>

class PciRootHostTests : public zxtest::Test {
 protected:
  void SetUp() final {
    ASSERT_OK(fake_root_resource_create(fake_root_.reset_and_get_address()));
    root_host_.reset(new PciRootHost(fake_root_.borrow()));
  }

  zx::resource& fake_root() { return fake_root_; }
  PciRootHost& root_host() { return *root_host_.get(); }

 private:
  zx::resource fake_root_;
  std::unique_ptr<PciRootHost> root_host_;
};

// The allocators backing the RootHost have their own tests inside the source
// directory of region-alloc, so there's no need to implement region rango
// tests in this suite. The resource reaping is the important detail to test
TEST_F(PciRootHostTests, ResourceAllocationLifecycle) {
  const zx_paddr_t kRangeStart = 0x1000;
  const size_t kRangeSize = 0x4000;
  ASSERT_OK(root_host().Mmio64().AddRegion({0, 0x100000000}));
  {
    zx::resource res1, res2, res3;
    zx::eventpair endpoint1, endpoint2, endpoint3;
    // Allocate at a given position.
    ASSERT_OK(root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res1, &endpoint1));
    // That position should not work.
    ASSERT_EQ(ZX_ERR_NOT_FOUND,
              root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res2, &endpoint2));
    // But an allocation of the same size with no base should.
    ASSERT_OK(root_host().AllocateMmio64Window(0, kRangeSize, &res3, &endpoint3));
  }
  // The allocate regions should be cleared out, so the specific range is free again.
  zx::resource res;
  zx::eventpair endpoint;
  ASSERT_OK(root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res, &endpoint));
}

TEST_F(PciRootHostTests, Mcfg) {
  McfgAllocation in_mcfg = {
      .address = 0x100000000,
      .pci_segment = 1,
      .start_bus_number = 0,
      .end_bus_number = 64,
  };
  McfgAllocation out_mcfg;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, root_host().GetSegmentMcfgAllocation(in_mcfg.pci_segment, &out_mcfg));
  root_host().mcfgs().push_back(in_mcfg);
  ASSERT_EQ(ZX_OK, root_host().GetSegmentMcfgAllocation(in_mcfg.pci_segment, &out_mcfg));
  ASSERT_BYTES_EQ(&in_mcfg, &out_mcfg, sizeof(McfgAllocation));
}

TEST_F(PciRootHostTests, MsiAllocationTest) {
  const uint32_t irq_cnt = 8;
  zx::msi msi = {};
  ASSERT_OK(root_host().AllocateMsi(irq_cnt, &msi));
  zx_info_msi_t info;
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(info.num_irq, irq_cnt);
  ASSERT_EQ(info.interrupt_count, 0);
}
