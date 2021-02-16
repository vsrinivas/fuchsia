// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <zircon/hw/pci.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"

class FakePciProtocolTests : public zxtest::Test {
 protected:
  void SetUp() final {
    fake_pci_.reset();
    pci_ = ddk::PciProtocolClient(&fake_pci_.get_protocol());
  }
  pci::FakePciProtocol& fake_pci() { return fake_pci_; }
  ddk::PciProtocolClient& pci() { return pci_; }

 private:
  pci::FakePciProtocol fake_pci_;
  ddk::PciProtocolClient pci_;
};

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
  zx_pcie_device_info_t actual{};
  zx_pcie_device_info_t zeroed{};
  ASSERT_OK(pci().GetDeviceInfo(&actual));
  ASSERT_EQ(0, memcmp(&zeroed, &actual, sizeof(zeroed)));

  zx_pcie_device_info_t expected = {
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
