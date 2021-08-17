// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/pci.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/hard_int.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/capabilities/msix.h"
#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_allocator.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_config.h"

namespace pci {

class PciCapabilityTests : public zxtest::Test {
 public:
  PciCapabilityTests() = default;
  ddk::MmioBuffer& mmio() { return *mmio_; }
  ddk::MmioView view() { return mmio_->View(0, mmio_->get_size()); }
  static pci_bdf_t bdf() { return {0, 0, 0}; }
  static Bar CreateBar(uint8_t bar_id, size_t size) {
    Bar bar = {
        .size = size,
        .bar_id = bar_id,
        .allocation = std::make_unique<FakeAllocation>(std::nullopt, size),
    };
    return bar;
  }

  void ConfigureMsixCapability(Config& cfg, uint8_t tbar, uint8_t pbar, zx_paddr_t toffset,
                               zx_paddr_t poffset, size_t vectors = 8) {
    MsixControlReg ctrl{};
    MsixTableReg table{};
    MsixPbaReg pba{};

    ctrl.set_table_size(vectors - 1);
    table.set_offset(toffset);
    table.set_bir(tbar);
    pba.set_offset(poffset);
    pba.set_bir(pbar);

    cfg.Write(PciReg16(MsixCapability::kMsixControlRegisterOffset), ctrl.value);
    cfg.Write(PciReg32(MsixCapability::kMsixTableRegisterOffset), table.reg_value());
    cfg.Write(PciReg32(MsixCapability::kMsixPbaRegisterOffset), pba.reg_value());
  }

 protected:
  void SetUp() final {
    zx::vmo vmo;
    ZX_ASSERT(zx::vmo::create(PCI_BASE_CONFIG_SIZE, /*options=*/0, &vmo) == ZX_OK);
    ZX_ASSERT(ddk::MmioBuffer::Create(0, PCI_BASE_CONFIG_SIZE, std::move(vmo),
                                      ZX_CACHE_POLICY_UNCACHED, &mmio_) == ZX_OK);
  }

  void TearDown() final { mmio_->reset(); }

 private:
  std::optional<ddk::MmioBuffer> mmio_;
};

TEST_F(PciCapabilityTests, FixtureTest) {
  MmioConfig cfg = FakeMmioConfig(bdf(), view());
  size_t vectors = 8;
  uint8_t tbar = 1;
  uint8_t pbar = 2;
  zx_paddr_t toffset = 0x4000;
  zx_paddr_t poffset = 0x8000;
  ConfigureMsixCapability(cfg, tbar, pbar, toffset, poffset, vectors);
  ASSERT_EQ(mmio().Read16(MsixCapability::kMsixControlRegisterOffset), vectors - 1);
  ASSERT_EQ(mmio().Read32(MsixCapability::kMsixTableRegisterOffset), toffset | tbar);
  ASSERT_EQ(mmio().Read32(MsixCapability::kMsixPbaRegisterOffset), poffset | pbar);
}

TEST_F(PciCapabilityTests, InitTest) {
  MmioConfig cfg = FakeMmioConfig(bdf(), view());
  MsixCapability msix(cfg, 0);
  ConfigureMsixCapability(cfg, /*tbar=*/1, /*pbar=*/1, /*toffset=*/0x4000, /*poffset=*/0x8000);
  Bar bar = CreateBar(1, 0xC000);
  // Catch double initializations.
  ASSERT_OK(msix.Init(bar, bar));
  ASSERT_STATUS(msix.Init(bar, bar), ZX_ERR_BAD_STATE);
}

TEST_F(PciCapabilityTests, MsixBarAccessTest) {
  Bar bar1 = CreateBar(1, 0x4000);
  Bar bar2 = CreateBar(2, 0x1000);

  MmioConfig cfg = FakeMmioConfig(bdf(), view());
  // Simple test, everything aligns well in one bar.
  {
    ConfigureMsixCapability(cfg, 1, 1, 0x2000, 0x3000);
    MsixCapability msix(cfg, 0);
    ASSERT_OK(msix.Init(bar1, bar1));
    ASSERT_EQ(0x2000, msix.GetBarDataSize(bar1).value());
  }

  // Swap tbar and pbar to ensure the ordering check is correct.
  {
    ConfigureMsixCapability(cfg, 1, 1, 0x3000, 0x2000);
    MsixCapability msix(cfg, 0);
    ASSERT_OK(msix.Init(bar1, bar1));
    ASSERT_EQ(0x2000, msix.GetBarDataSize(bar1).value());
  }

  // Different bars, Tbar should work but Pbar will be denied.
  {
    ConfigureMsixCapability(cfg, 1, 2, 0x1000, 0x0);
    MsixCapability msix(cfg, 0);
    ASSERT_OK(msix.Init(bar1, bar2));
    ASSERT_EQ(0x1000, msix.GetBarDataSize(bar1).value());
    ASSERT_STATUS(ZX_ERR_ACCESS_DENIED, msix.GetBarDataSize(bar2).status_value());
  }

  // Verify data sharing the same page is denied.
  {
    ConfigureMsixCapability(cfg, 1, 1, 0x800, 0x1000);
    MsixCapability msix(cfg, 0);
    ASSERT_OK(msix.Init(bar1, bar1));
    ASSERT_STATUS(ZX_ERR_ACCESS_DENIED, msix.GetBarDataSize(bar1).status_value());
  }

  // Ensure a device cannot access data when a table is not aligned to a page.
  {
    uint32_t page_size = zx_system_get_page_size();
    ConfigureMsixCapability(cfg, 1, 1, page_size + 0x100, page_size + 0x200);
    MsixCapability msix(cfg, 0);
    ASSERT_OK(msix.Init(bar1, bar1));
    ASSERT_EQ(page_size, msix.GetBarDataSize(bar1).value());
  }
}

}  // namespace pci
