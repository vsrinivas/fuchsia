// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-resource/resource.h>
#include <lib/pci/root.h>
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
    root_host_.Init(fake_root_.get());
  }

  const zx::resource& fake_root() { return fake_root_; }
  PciRootHost& root_host() { return root_host_; }

 private:
  zx::resource fake_root_;
  PciRootHost root_host_;
};

// The allocators backing the RootHost have their own tests inside the source
// directory of region-alloc, so there's no need to implement region rango
// tests in this suite. The resource reaping is the important detail to test.
TEST_F(PciRootHostTests, ResourceAllocationLifecycle) {
  const zx_paddr_t kRangeStart = 0x0;
  const size_t kRangeSize = 0xC000;
  ASSERT_OK(root_host().Mmio64().AddRegion({kRangeStart, kRangeSize}));
  zx::resource res;
  {
    zx::eventpair endpoint1, endpoint2;
    ASSERT_OK(root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res, &endpoint1));
    ASSERT_EQ(ZX_ERR_NOT_FOUND,
              root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res, &endpoint2));
  }
  zx::eventpair endpoint;
  ASSERT_OK(root_host().AllocateMmio64Window(kRangeStart, kRangeSize, &res, &endpoint));
}

TEST_F(PciRootHostTests, Initialize) {
  // The fixture will already have initialized the Root Host, so ensure we can't re-init.
  ASSERT_DEATH([this]() { root_host().Init(fake_root().get()); });
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
