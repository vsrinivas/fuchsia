// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/syscalls/pci.h>

#include <zxtest/zxtest.h>

#include "fuchsia/hardware/pci/c/banjo.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "zircon/system/public/zircon/syscalls.h"

class FakePciProtocolTests : public zxtest::Test {
 protected:
  void SetUp() final {
    fake_pci_.Reset();
    pci_ = ddk::PciProtocolClient(&fake_pci_.get_protocol());
  }
  pci::FakePciProtocol& fake_pci() { return fake_pci_; }
  ddk::PciProtocolClient& pci() { return pci_; }

 private:
  pci::FakePciProtocol fake_pci_;
  ddk::PciProtocolClient pci_;
};

TEST_F(FakePciProtocolTests, CreateBar) {
  zx::vmo vmo;
  size_t size = 8193;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));
  fake_pci().CreateBar(0, size, true);

  pci_bar_t bar;
  pci().GetBar(0, &bar);
  EXPECT_EQ(size, bar.size);
}

TEST_F(FakePciProtocolTests, ResetDevice) {
  uint32_t reset_cnt = 0;
  ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount());
  ASSERT_OK(pci().ResetDevice());
  ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount());
  ASSERT_OK(pci().ResetDevice());
  ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount());
}

TEST_F(FakePciProtocolTests, GetBti) {
  zx::bti bti{};

  ASSERT_OK(pci().GetBti(0, &bti));
  zx_info_bti_t info;
  // Verify it's a BTI at least.
  ASSERT_OK(bti.get_info(ZX_INFO_BTI, &info, sizeof(info), /*actual_count=*/nullptr,
                         /*avail_count=*/nullptr));
}

TEST_F(FakePciProtocolTests, EnableBusMaster) {
  // If enable has never been called there should be no value.
  ASSERT_FALSE(fake_pci().GetBusMasterEnabled().has_value());

  ASSERT_OK(pci().EnableBusMaster(true));
  ASSERT_TRUE(fake_pci().GetBusMasterEnabled().value());

  ASSERT_OK(pci().EnableBusMaster(false));
  ASSERT_FALSE(fake_pci().GetBusMasterEnabled().value());
}

TEST_F(FakePciProtocolTests, GetDeviceInfo) {
  pcie_device_info_t actual{};
  pcie_device_info_t zeroed{};
  ASSERT_OK(pci().GetDeviceInfo(&actual));
  ASSERT_EQ(0, memcmp(&zeroed, &actual, sizeof(zeroed)));

  pcie_device_info_t expected = {
      .vendor_id = 0x1,
      .device_id = 0x2,

      .base_class = 0x3,
      .sub_class = 0x4,
      .program_interface = 0x5,
      .revision_id = 0x6,

      .bus_id = 0x7,
      .dev_id = 0x8,
      .func_id = 0x9,
  };

  fake_pci().SetDeviceInfo(expected);
  ASSERT_OK(pci().GetDeviceInfo(&actual));
  ASSERT_EQ(0, memcmp(&expected, &actual, sizeof(expected)));

  // Did we update the config header to match the device structure?
  uint8_t val8;
  uint16_t val16;
  ASSERT_OK(pci().ConfigRead16(PCI_CFG_VENDOR_ID, &val16));
  ASSERT_EQ(expected.vendor_id, val16);
  ASSERT_OK(pci().ConfigRead16(PCI_CFG_DEVICE_ID, &val16));
  ASSERT_EQ(expected.device_id, val16);
  ASSERT_OK(pci().ConfigRead8(PCI_CFG_REVISION_ID, &val8));
  ASSERT_EQ(expected.revision_id, val8);
  ASSERT_OK(pci().ConfigRead8(PCI_CFG_CLASS_CODE_BASE, &val8));
  ASSERT_EQ(expected.base_class, val8);
  ASSERT_OK(pci().ConfigRead8(PCI_CFG_CLASS_CODE_SUB, &val8));
  ASSERT_EQ(expected.sub_class, val8);
  ASSERT_OK(pci().ConfigRead8(PCI_CFG_CLASS_CODE_INTR, &val8));
  ASSERT_EQ(expected.program_interface, val8);
}

TEST_F(FakePciProtocolTests, QueryIrqMode) {
  uint32_t irq_cnt = 0;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, pci().QueryIrqMode(PCI_IRQ_MODE_LEGACY, &irq_cnt));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, pci().QueryIrqMode(PCI_IRQ_MODE_MSI, &irq_cnt));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, pci().QueryIrqMode(PCI_IRQ_MODE_MSI_X, &irq_cnt));

  fake_pci().AddLegacyInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_LEGACY, &irq_cnt));
  ASSERT_EQ(1, irq_cnt);

  // MSI supports interrupt configuration via powers of two, so ensure that we
  // round down if not enough have been added.
  fake_pci().AddMsiInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI, &irq_cnt));
  ASSERT_EQ(1, irq_cnt);
  fake_pci().AddMsiInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI, &irq_cnt));
  ASSERT_EQ(2, irq_cnt);
  fake_pci().AddMsiInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI, &irq_cnt));
  ASSERT_EQ(2, irq_cnt);
  fake_pci().AddMsiInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI, &irq_cnt));
  ASSERT_EQ(4, irq_cnt);

  // MSI-X doesn't care about alignment, so any value should work.
  fake_pci().AddMsixInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI_X, &irq_cnt));
  ASSERT_EQ(1, irq_cnt);
  fake_pci().AddMsixInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI_X, &irq_cnt));
  ASSERT_EQ(2, irq_cnt);
  fake_pci().AddMsixInterrupt();
  ASSERT_EQ(ZX_OK, pci().QueryIrqMode(PCI_IRQ_MODE_MSI_X, &irq_cnt));
  ASSERT_EQ(3, irq_cnt);
}

TEST_F(FakePciProtocolTests, SetInterruptMode) {
  fake_pci().AddLegacyInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsixInterrupt();
  fake_pci().AddMsixInterrupt();

  pci_irq_mode_t mode = PCI_IRQ_MODE_LEGACY;
  ASSERT_OK(pci().SetInterruptMode(mode, 1));
  ASSERT_EQ(1, fake_pci().GetIrqCount());
  ASSERT_EQ(mode, fake_pci().GetIrqMode());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().SetInterruptMode(mode, 2));

  mode = PCI_IRQ_MODE_MSI;
  ASSERT_OK(pci().SetInterruptMode(mode, 1));
  ASSERT_EQ(1, fake_pci().GetIrqCount());
  ASSERT_EQ(mode, fake_pci().GetIrqMode());

  ASSERT_OK(pci().SetInterruptMode(mode, 2));
  ASSERT_EQ(2, fake_pci().GetIrqCount());
  ASSERT_EQ(mode, fake_pci().GetIrqMode());

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().SetInterruptMode(mode, 3));
  ASSERT_EQ(2, fake_pci().GetIrqCount());
  ASSERT_EQ(mode, fake_pci().GetIrqMode());

  ASSERT_OK(pci().SetInterruptMode(mode, 4));
  ASSERT_EQ(4, fake_pci().GetIrqCount());
  ASSERT_EQ(mode, fake_pci().GetIrqMode());
}

namespace {
// When interrupts are added to the fake a borrowed copy of the interrupt is
// returned for comparison by tests later. Its koid should match the koid of the
// duplicated handle returned by MapInterrupt.
template <typename T>
bool MatchKoids(const zx::object<T>& first, const zx::object<T>& second) {
  zx_info_handle_basic finfo{}, sinfo{};
  ZX_ASSERT(first.get_info(ZX_INFO_HANDLE_BASIC, &finfo, sizeof(finfo), nullptr, nullptr) == ZX_OK);
  ZX_ASSERT(second.get_info(ZX_INFO_HANDLE_BASIC, &sinfo, sizeof(sinfo), nullptr, nullptr) ==
            ZX_OK);

  return finfo.koid == sinfo.koid;
}
}  // namespace

TEST_F(FakePciProtocolTests, MapInterrupt) {
  // One notable difference between this fake and the real PCI protocol is that
  // it is an error to call SetInterruptMode and switch modes if an existing MSI is
  // mapped still. In the fake though, it's fine to do so. Switching IRQ modes
  // is not something drivers do in practice, so it's fine if they encounter
  // ZX_ERR_BAD_STATE at runtime if documentation details it.
  zx::interrupt& legacy = fake_pci().AddLegacyInterrupt();
  zx::interrupt& msi0 = fake_pci().AddMsiInterrupt();
  zx::interrupt& msi1 = fake_pci().AddMsiInterrupt();
  zx::interrupt& msix0 = fake_pci().AddMsixInterrupt();
  zx::interrupt& msix1 = fake_pci().AddMsixInterrupt();
  zx::interrupt& msix2 = fake_pci().AddMsixInterrupt();

  zx::interrupt interrupt{};
  uint32_t irq_cnt = 1;
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_TRUE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY_NOACK, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_TRUE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  irq_cnt = 2;
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_MSI, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_TRUE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(1, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_TRUE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  irq_cnt = 3;
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_MSI_X, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_TRUE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(1, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_TRUE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(2, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_TRUE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
}

TEST_F(FakePciProtocolTests, VerifyAllocatedMsis) {
  fake_pci().AddLegacyInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsiInterrupt();
  fake_pci().AddMsixInterrupt();

  zx::interrupt zero, one;
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_MSI, 2));
  ASSERT_OK(pci().MapInterrupt(0, &zero));
  ASSERT_OK(pci().MapInterrupt(1, &one));
  // Changing to other IRQ modes should be blocked because IRQ handles are outstanding.
  ASSERT_EQ(ZX_ERR_BAD_STATE, pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY, 1));
  ASSERT_EQ(ZX_ERR_BAD_STATE, pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY_NOACK, 1));
  ASSERT_EQ(ZX_ERR_BAD_STATE, pci().SetInterruptMode(PCI_IRQ_MODE_MSI_X, 1));
  zero.reset();
  one.reset();
  // Now transitioning should work.
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY, 1));
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_MSI_X, 1));

  // Verify MSI-X works the same.
  ASSERT_OK(pci().MapInterrupt(0, &zero));
  ASSERT_EQ(ZX_ERR_BAD_STATE, pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY, 1));
  zero.reset();
  ASSERT_OK(pci().SetInterruptMode(PCI_IRQ_MODE_LEGACY, 1));
}

TEST_F(FakePciProtocolTests, ConfigRW) {
  auto config = fake_pci().GetConfigVmo();

  // Verify the header space range. Reads can read the header [0, 63], but
  // writes cannot. All IO must fit within the config space [0, 255].
  uint8_t val8;
  ASSERT_DEATH([&]() { pci().ConfigWrite8(0, 0xFF); });
  ASSERT_NO_DEATH([&]() { pci().ConfigRead8(0, &val8); });
  ASSERT_DEATH([&]() { pci().ConfigWrite8(PCI_CFG_HEADER_SIZE - 1, 0xFF); });
  ASSERT_NO_DEATH([&]() { pci().ConfigRead8(PCI_CFG_HEADER_SIZE - 1, &val8); });
  // The ensures we also verify that offset + read/write size is within bounds.
  uint32_t val32;
  ASSERT_DEATH([&]() { pci().ConfigWrite32(PCI_BASE_CONFIG_SIZE - 2, 0xFF); });
  ASSERT_DEATH([&]() { pci().ConfigRead32(PCI_BASE_CONFIG_SIZE - 2, &val32); });

  for (uint16_t off = PCI_CFG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE; off++) {
    uint8_t val8;
    pci().ConfigWrite8(off, off);
    pci().ConfigRead8(off, &val8);
    ASSERT_EQ(off, val8);
    ASSERT_OK(config->read(&val8, off, sizeof(val8)));
    ASSERT_EQ(off, val8);
  }

  for (uint16_t off = PCI_CFG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE - 1; off++) {
    uint16_t val16;
    pci().ConfigWrite16(off, off);
    pci().ConfigRead16(off, &val16);
    ASSERT_EQ(off, val16);
    ASSERT_OK(config->read(&val16, off, sizeof(val16)));
    ASSERT_EQ(off, val16);
  }

  for (uint16_t off = PCI_CFG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE - 3; off++) {
    uint32_t val32;
    pci().ConfigWrite32(off, off);
    pci().ConfigRead32(off, &val32);
    ASSERT_EQ(off, val32);
    ASSERT_OK(config->read(&val32, off, sizeof(val32)));
    ASSERT_EQ(off, val32);
  }
}

TEST_F(FakePciProtocolTests, GetBar) {
  pci_bar_t bar{};
  ASSERT_EQ(ZX_ERR_NOT_FOUND, pci().GetBar(0, &bar));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().GetBar(6, &bar));

  uint32_t bar_id = 3;
  size_t size = 256;
  ASSERT_NO_DEATH([&]() { fake_pci().CreateBar(bar_id, size, true); });
  // Verify that the VMO we got back via the protocol method matches the setup
  // and that the other fields are correct.
  ASSERT_OK(pci().GetBar(bar_id, &bar));
  zx::vmo proto(bar.handle);
  zx::vmo& borrowed = fake_pci().GetBar(bar_id);
  ASSERT_TRUE(MatchKoids(borrowed, proto));
  ASSERT_EQ(bar_id, bar.id);
  ASSERT_EQ(size, bar.size);
}

TEST_F(FakePciProtocolTests, BarTypes) {
  size_t page_size = zx_system_get_page_size();
  fake_pci().CreateBar(0, page_size, true);
  fake_pci().CreateBar(1, page_size, false);

  pci_bar_t bar;
  ASSERT_OK(pci().GetBar(0, &bar));
  ASSERT_EQ(bar.type, ZX_PCI_BAR_TYPE_MMIO);
  ASSERT_OK(pci().GetBar(1, &bar));
  ASSERT_EQ(bar.type, ZX_PCI_BAR_TYPE_PIO);
}

TEST_F(FakePciProtocolTests, MapMmio) {
  const uint32_t bar_id = 0;
  const uint64_t bar_size = 256;
  fake_pci().CreateBar(bar_id, bar_size, true);
  zx::vmo& borrowed = fake_pci().GetBar(bar_id);

  // Ensure that our fake implementation / backend for the BAR methods still works with
  // the MapMmio helper method added to device-protocol.
  ddk::Pci dp_pci(fake_pci().get_protocol());
  std::optional<ddk::MmioBuffer> mmio = std::nullopt;
  ASSERT_OK(dp_pci.MapMmio(bar_id, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
  ASSERT_TRUE(MatchKoids(borrowed, *mmio->get_vmo()));
}

TEST_F(FakePciProtocolTests, Capabilities) {
  // Try invalid capabilities.
  ASSERT_DEATH([&]() { fake_pci().AddCapability(0, PCI_CFG_HEADER_SIZE, 16); });
  ASSERT_DEATH([&]() {
    fake_pci().AddCapability(PCI_CAP_ID_FLATTENING_PORTAL_BRIDGE + 1, PCI_CFG_HEADER_SIZE, 16);
  });

  // Try invalid locations.
  ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(PCI_CFG_HEADER_SIZE - 16, 32); });
  ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(PCI_BASE_CONFIG_SIZE - 16, 32); });

  // Overlap tests.
  ASSERT_NO_DEATH([&]() { fake_pci().AddVendorCapability(0xB0, 16); });
  ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0 + 8, 16); });
  ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0 - 8, 16); });
  ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0, 32); });
}

TEST_F(FakePciProtocolTests, PciGetFirstAndNextCapability) {
  auto config = fake_pci().GetConfigVmo();
  // The first capability should set up the capabilities pointer.
  fake_pci().AddVendorCapability(0x50, 6);
  uint8_t offset1 = 0;
  ASSERT_OK(pci().GetFirstCapability(PCI_CAP_ID_VENDOR, &offset1));
  uint8_t val;
  config->read(&val, PCI_CFG_CAPABILITIES_PTR, sizeof(val));
  ASSERT_EQ(0x50, val);
  config->read(&val, offset1, sizeof(val));
  ASSERT_EQ(PCI_CAP_ID_VENDOR, val);
  config->read(&val, offset1 + 2, sizeof(val));
  ASSERT_EQ(6, val);

  // After adding the new capability we need to check that the previous next pointer was set up.
  fake_pci().AddVendorCapability(0x60, 8);
  config->read(&val, 0x51, sizeof(val));
  ASSERT_EQ(val, 0x60);

  // Can we find sequential capabilites, or different IDs?
  uint8_t offset2 = 0;
  ASSERT_OK(pci().GetNextCapability(PCI_CAP_ID_VENDOR, offset1, &offset2));
  ASSERT_EQ(0x60, offset2);

  fake_pci().AddPciExpressCapability(0x70);
  fake_pci().AddVendorCapability(0xB0, 16);

  ASSERT_OK(pci().GetFirstCapability(PCI_CAP_ID_PCI_EXPRESS, &offset1));
  ASSERT_EQ(0x70, offset1);

  ASSERT_OK(pci().GetNextCapability(PCI_CAP_ID_VENDOR, offset2, &offset1));
  ASSERT_EQ(0xB0, offset1);
}
