// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-resource/resource.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>
#include <lib/pci/pciroot.h>
#include <lib/pci/root_host.h>
#include <lib/zx/resource.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

class TestPciroot final : public PcirootBase, public inspect::InspectTestHelper {
 public:
  TestPciroot(PciRootHost* root_host, zx_device_t* parent, const char* name)
      : PcirootBase(root_host, parent, name) {}
  ~TestPciroot() final = default;
};

class PcirootTests : public zxtest::Test {
 public:
  PcirootTests() {
    ASSERT_OK(fake_root_resource_create(fake_root_resource_.reset_and_get_address()));
  }
  PciRootHost& root_host() { return *root_host_; }

 protected:
  void SetUp() final {
    root_host_ = std::make_unique<PciRootHost>(fake_root_resource_.borrow(), PCI_ADDRESS_SPACE_IO);
  }

 private:
  zx::resource fake_root_resource_;
  std::unique_ptr<PciRootHost> root_host_;
};

// Tables copied from QEMU x64 and represent [base, base + size).
// clang-format off
using PciRegion = std::pair<uint64_t, size_t>;
constexpr PciRegion kTestAllocatedIo[] {
  {0xc080, 0xc0a0},
  {0xc0a0, 0xc0c0},
  {0xc000, 0xc040},
  {0xc0c0, 0xc0e0},
  {0x700, 0x740},
};
constexpr PciRegion kTestAllocatedMmio[] = {
  {0xfebfc000, 0xfec00000},
  {0xfebe0000, 0xfebe1000},
  {0xfebc0000, 0xfebe0000},
  {0xfebe1000, 0xfebe2000},
};
constexpr PciRegion kTestBoardIo[] = {
  {0, 0x60},
  {0x61, 0x64},
  {0x65, 0x70},
  {0x78, 0x378},
  {0x380, 0x3f8},
  {0x400, 0x510},
  {0x51c, 0x620},
  {0x630, 0xcc0},
  {0xce4, 0xcf8},
  {0xd00, 0x10000},
};
constexpr PciRegion kTestBoardMmio[] = {
  {0x80000000, 0xb0000000},
  {0xc0000000, 0xfec00000},
};
constexpr PciRegion kTestBoardMmio64[] = {
  {0x280000000, 0xa80000000},
};
// clang-format on

TEST_F(PcirootTests, Inspect) {
  // Set up the board regions as necessary so they're available before the
  // Pciroot initialization path that records board values.
  for (auto& region : kTestBoardIo) {
    ASSERT_OK(root_host().Io().AddRegion({region.first, region.second - region.first}));
  }
  for (auto& region : kTestBoardMmio) {
    ASSERT_OK(root_host().Mmio32().AddRegion({region.first, region.second - region.first}));
  }
  for (auto& region : kTestBoardMmio64) {
    ASSERT_OK(root_host().Mmio64().AddRegion({region.first, region.second - region.first}));
  }

  auto parent = MockDevice::FakeRootParent();
  TestPciroot pciroot(&root_host(), parent.get(), "TestPciroot");
  // Pull out the expected allocations.
  zx::resource resource{};
  zx::eventpair ep{};
  zx_paddr_t out_base;
  std::vector<std::pair<zx::resource, zx::eventpair>> bookkeeping;
  for (auto& region : kTestAllocatedIo) {
    ASSERT_OK(pciroot.PcirootGetAddressSpace(region.first, region.second - region.first,
                                             PCI_ADDRESS_SPACE_IO, false, &out_base, &resource,
                                             &ep));
    bookkeeping.emplace_back(std::move(resource), std::move(ep));
  }
  for (auto& region : kTestAllocatedMmio) {
    ASSERT_OK(pciroot.PcirootGetAddressSpace(region.first, region.second - region.first,
                                             PCI_ADDRESS_SPACE_MEMORY, true, &out_base, &resource,
                                             &ep));
    bookkeeping.emplace_back(std::move(resource), std::move(ep));
  }

  // Ensure that the number of properties of each type match up after making the
  // protocol calls rather than the raw strings themselves.
  ASSERT_NO_FATAL_FAILURE(pciroot.ReadInspect(pciroot.inspect().DuplicateVmo()));
  ASSERT_EQ(std::size(kTestAllocatedIo), pciroot.hierarchy()
                                             .GetByPath({PcirootInspect::kAllocatedIoName})
                                             ->node()
                                             .properties()
                                             .size());
  ASSERT_EQ(std::size(kTestAllocatedMmio), pciroot.hierarchy()
                                               .GetByPath({PcirootInspect::kAllocatedMmioName})
                                               ->node()
                                               .properties()
                                               .size());
  ASSERT_EQ(
      std::size(kTestBoardIo),
      pciroot.hierarchy().GetByPath({PcirootInspect::kBoardIoName})->node().properties().size());
  // In inspect we have one mmio region, rather than two in the Pciroot regions.
  ASSERT_EQ(
      std::size(kTestBoardMmio) + std::size(kTestBoardMmio64),
      pciroot.hierarchy().GetByPath({PcirootInspect::kBoardMmioName})->node().properties().size());
}
