// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/status.h>
#include <zircon/limits.h>

#include <ddktl/protocol/pciroot.h>
#include <zxtest/zxtest.h>

#include "../../config.h"
#include "../fakes/fake_pciroot.h"

namespace pci {

class PciConfigTests : public zxtest::Test {
 public:
  FakePciroot& pciroot_proto() { return *pciroot_; }
  ddk::PcirootProtocolClient& pciroot_client() { return *client_; }

  const pci_bdf_t default_bdf1() { return {0, 1, 2}; }
  const pci_bdf_t default_bdf2() { return {1, 2, 3}; }

 protected:
  void SetUp() final {
    pciroot_.reset(new FakePciroot(0, 1));
    client_ = std::make_unique<ddk::PcirootProtocolClient>(pciroot_->proto());
  }
  void TearDown() final { pciroot_->ecam().reset(); }
  void ConfigReadWriteImpl(Config* cfg);
  void IntegrationTestImpl(Config* cfg1, Config* cfg2);

 private:
  std::unique_ptr<FakePciroot> pciroot_;
  std::unique_ptr<ddk::PcirootProtocolClient> client_;
};

void PciConfigTests::IntegrationTestImpl(Config* cfg1, Config* cfg2) {
  {
    FakePciType0Config& dev = pciroot_proto().ecam().get(default_bdf1()).device;
    dev.set_vendor_id(0x8086)
        .set_device_id(0x1234)
        .set_header_type(0x01)
        .set_revision_id(12)
        .set_expansion_rom_address(0xFF0000EE);
    // Test 8, 16, and 32 bit reads.
    EXPECT_EQ(cfg1->Read(Config::kRevisionId), dev.revision_id());
    EXPECT_EQ(cfg1->Read(Config::kVendorId), dev.vendor_id());
    EXPECT_EQ(cfg1->Read(Config::kDeviceId), dev.device_id());
    EXPECT_EQ(cfg1->Read(Config::kHeaderType), dev.header_type());
    EXPECT_EQ(cfg1->Read(Config::kExpansionRomAddress), dev.expansion_rom_address());
  }
  // Now try the same thing for a different, unconfigured device and ensure they aren't
  // overlapping somehow.
  {
    FakePciType0Config& dev = pciroot_proto().ecam().get(default_bdf2()).device;
    EXPECT_EQ(cfg2->Read(Config::kRevisionId), 0x0);
    EXPECT_EQ(cfg2->Read(Config::kVendorId), 0xFFFF);
    EXPECT_EQ(cfg2->Read(Config::kDeviceId), 0xFFFF);
    EXPECT_EQ(cfg2->Read(Config::kHeaderType), 0x0);
    EXPECT_EQ(cfg2->Read(Config::kExpansionRomAddress), 0x0);

    dev.set_vendor_id(0x8680)
        .set_device_id(0x4321)
        .set_header_type(0x02)
        .set_revision_id(3)
        .set_expansion_rom_address(0xFF0000EE);

    EXPECT_EQ(cfg2->Read(Config::kRevisionId), dev.revision_id());
    EXPECT_EQ(cfg2->Read(Config::kVendorId), dev.vendor_id());
    EXPECT_EQ(cfg2->Read(Config::kDeviceId), dev.device_id());
    EXPECT_EQ(cfg2->Read(Config::kHeaderType), dev.header_type());
    EXPECT_EQ(cfg2->Read(Config::kExpansionRomAddress), dev.expansion_rom_address());
  }
}

void PciConfigTests::ConfigReadWriteImpl(Config* cfg) {
  FakePciType0Config& dev = pciroot_proto().ecam().get(default_bdf1()).device;
  ASSERT_EQ(dev.vendor_id(), 0xFFFF);
  ASSERT_EQ(dev.device_id(), 0xFFFF);
  ASSERT_EQ(dev.command(), 0x0);
  ASSERT_EQ(dev.status(), 0x0);
  ASSERT_EQ(dev.revision_id(), 0x0);
  ASSERT_EQ(dev.program_interface(), 0x0);
  ASSERT_EQ(dev.sub_class(), 0x0);
  ASSERT_EQ(dev.base_class(), 0x0);
  ASSERT_EQ(dev.cache_line_size(), 0x0);
  ASSERT_EQ(dev.latency_timer(), 0x0);
  ASSERT_EQ(dev.header_type(), 0x0);
  ASSERT_EQ(dev.bist(), 0x0);
  ASSERT_EQ(dev.cardbus_cis_ptr(), 0x0);
  ASSERT_EQ(dev.subsystem_vendor_id(), 0x0);
  ASSERT_EQ(dev.subsystem_id(), 0x0);
  ASSERT_EQ(dev.expansion_rom_address(), 0x0);
  ASSERT_EQ(dev.capabilities_ptr(), 0x0);
  ASSERT_EQ(dev.interrupt_line(), 0x0);
  ASSERT_EQ(dev.interrupt_pin(), 0x0);
  ASSERT_EQ(dev.min_grant(), 0x0);
  ASSERT_EQ(dev.max_latency(), 0x0);

  // Ensure the config header reads match the reset values above, this time
  // through the config interface.
  EXPECT_EQ(cfg->Read(Config::kVendorId), 0xFFFF);
  EXPECT_EQ(cfg->Read(Config::kDeviceId), 0xFFFF);
  EXPECT_EQ(cfg->Read(Config::kCommand), 0x0);
  EXPECT_EQ(cfg->Read(Config::kStatus), 0x0);
  EXPECT_EQ(cfg->Read(Config::kRevisionId), 0x0);
  EXPECT_EQ(cfg->Read(Config::kProgramInterface), 0x0);
  EXPECT_EQ(cfg->Read(Config::kSubClass), 0x0);
  EXPECT_EQ(cfg->Read(Config::kBaseClass), 0x0);
  EXPECT_EQ(cfg->Read(Config::kCacheLineSize), 0x0);
  EXPECT_EQ(cfg->Read(Config::kLatencyTimer), 0x0);
  EXPECT_EQ(cfg->Read(Config::kHeaderType), 0x0);
  EXPECT_EQ(cfg->Read(Config::kBist), 0x0);
  EXPECT_EQ(cfg->Read(Config::kCardbusCisPtr), 0x0);
  EXPECT_EQ(cfg->Read(Config::kSubsystemVendorId), 0x0);
  EXPECT_EQ(cfg->Read(Config::kSubsystemId), 0x0);
  EXPECT_EQ(cfg->Read(Config::kExpansionRomAddress), 0x0);
  EXPECT_EQ(cfg->Read(Config::kCapabilitiesPtr), 0x0);
  EXPECT_EQ(cfg->Read(Config::kInterruptLine), 0x0);
  EXPECT_EQ(cfg->Read(Config::kInterruptPin), 0x0);
  EXPECT_EQ(cfg->Read(Config::kMinGrant), 0x0);
  EXPECT_EQ(cfg->Read(Config::kMaxLatency), 0x0);

  // Write test data to the config header registers.
  cfg->Write(Config::kVendorId, 0x1111);
  cfg->Write(Config::kDeviceId, 0x2222);
  cfg->Write(Config::kCommand, 0x3333);
  cfg->Write(Config::kStatus, 0x4444);
  cfg->Write(Config::kRevisionId, 0x55);
  cfg->Write(Config::kProgramInterface, 0x66);
  cfg->Write(Config::kSubClass, 0x77);
  cfg->Write(Config::kBaseClass, 0x88);
  cfg->Write(Config::kCacheLineSize, 0x99);
  cfg->Write(Config::kLatencyTimer, 0xAA);
  cfg->Write(Config::kHeaderType, 0xBB);
  cfg->Write(Config::kBist, 0xCC);
  cfg->Write(Config::kCardbusCisPtr, 0xDDDDDDDD);
  cfg->Write(Config::kSubsystemVendorId, 0xEEEE);
  cfg->Write(Config::kSubsystemId, 0xFFFF);
  cfg->Write(Config::kExpansionRomAddress, 0x11111111);
  cfg->Write(Config::kCapabilitiesPtr, 0x22);
  cfg->Write(Config::kInterruptLine, 0x33);
  cfg->Write(Config::kInterruptPin, 0x44);
  cfg->Write(Config::kMinGrant, 0x55);
  cfg->Write(Config::kMaxLatency, 0x66);

  // Verify the config header reads match through the fake ecam.
  EXPECT_EQ(dev.vendor_id(), 0x1111);
  EXPECT_EQ(dev.device_id(), 0x2222);
  EXPECT_EQ(dev.command(), 0x3333);
  EXPECT_EQ(dev.status(), 0x4444);
  EXPECT_EQ(dev.revision_id(), 0x55);
  EXPECT_EQ(dev.program_interface(), 0x66);
  EXPECT_EQ(dev.sub_class(), 0x77);
  EXPECT_EQ(dev.base_class(), 0x88);
  EXPECT_EQ(dev.cache_line_size(), 0x99);
  EXPECT_EQ(dev.latency_timer(), 0xAA);
  EXPECT_EQ(dev.header_type(), 0xBB);
  EXPECT_EQ(dev.bist(), 0xCC);
  EXPECT_EQ(dev.cardbus_cis_ptr(), 0xDDDDDDDD);
  EXPECT_EQ(dev.subsystem_vendor_id(), 0xEEEE);
  EXPECT_EQ(dev.subsystem_id(), 0xFFFF);
  EXPECT_EQ(dev.expansion_rom_address(), 0x11111111);
  EXPECT_EQ(dev.capabilities_ptr(), 0x22);
  EXPECT_EQ(dev.interrupt_line(), 0x33);
  EXPECT_EQ(dev.interrupt_pin(), 0x44);
  EXPECT_EQ(dev.min_grant(), 0x55);
  EXPECT_EQ(dev.max_latency(), 0x66);

  // Verify the config header reads match through the config interface.
  EXPECT_EQ(cfg->Read(Config::kVendorId), 0x1111);
  EXPECT_EQ(cfg->Read(Config::kDeviceId), 0x2222);
  EXPECT_EQ(cfg->Read(Config::kCommand), 0x3333);
  EXPECT_EQ(cfg->Read(Config::kStatus), 0x4444);
  EXPECT_EQ(cfg->Read(Config::kRevisionId), 0x55);
  EXPECT_EQ(cfg->Read(Config::kProgramInterface), 0x66);
  EXPECT_EQ(cfg->Read(Config::kSubClass), 0x77);
  EXPECT_EQ(cfg->Read(Config::kBaseClass), 0x88);
  EXPECT_EQ(cfg->Read(Config::kCacheLineSize), 0x99);
  EXPECT_EQ(cfg->Read(Config::kLatencyTimer), 0xAA);
  EXPECT_EQ(cfg->Read(Config::kHeaderType), 0xBB);
  EXPECT_EQ(cfg->Read(Config::kBist), 0xCC);
  EXPECT_EQ(cfg->Read(Config::kCardbusCisPtr), 0xDDDDDDDD);
  EXPECT_EQ(cfg->Read(Config::kSubsystemVendorId), 0xEEEE);
  EXPECT_EQ(cfg->Read(Config::kSubsystemId), 0xFFFF);
  EXPECT_EQ(cfg->Read(Config::kExpansionRomAddress), 0x11111111);
  EXPECT_EQ(cfg->Read(Config::kCapabilitiesPtr), 0x22);
  EXPECT_EQ(cfg->Read(Config::kInterruptLine), 0x33);
  EXPECT_EQ(cfg->Read(Config::kInterruptPin), 0x44);
  EXPECT_EQ(cfg->Read(Config::kMinGrant), 0x55);
  EXPECT_EQ(cfg->Read(Config::kMaxLatency), 0x66);
}

TEST_F(PciConfigTests, MmioIntegration) {
  std::unique_ptr<Config> cfg1, cfg2;
  ASSERT_OK(MmioConfig::Create(default_bdf1(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg1));
  ASSERT_OK(MmioConfig::Create(default_bdf2(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg2));
  IntegrationTestImpl(cfg1.get(), cfg2.get());
}

TEST_F(PciConfigTests, MmioConfigReadWrite) {
  std::unique_ptr<Config> cfg;
  ASSERT_OK(MmioConfig::Create(default_bdf1(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  ConfigReadWriteImpl(cfg.get());
}

TEST_F(PciConfigTests, ProxyIntegration) {
  std::unique_ptr<Config> cfg1, cfg2;
  ASSERT_OK(ProxyConfig::Create(default_bdf1(), &pciroot_client(), &cfg1));
  ASSERT_OK(ProxyConfig::Create(default_bdf2(), &pciroot_client(), &cfg2));
  IntegrationTestImpl(cfg1.get(), cfg2.get());
}

TEST_F(PciConfigTests, ProxyConfigReadWrite) {
  std::unique_ptr<Config> cfg;
  ASSERT_OK(ProxyConfig::Create(default_bdf1(), &pciroot_client(), &cfg));
  ConfigReadWriteImpl(cfg.get());
}

TEST_F(PciConfigTests, ConfigGetView) {
  std::unique_ptr<Config> cfg;
  ASSERT_OK(ProxyConfig::Create(default_bdf1(), &pciroot_client(), &cfg));
  ASSERT_EQ(cfg->get_view().status_value(), ZX_ERR_NOT_SUPPORTED);
  cfg.reset();
  auto& ecam_mmio = pciroot_proto().ecam().mmio();
  ASSERT_OK(MmioConfig::Create(default_bdf2(), &ecam_mmio, /*start_bus=*/1, /*end_bus=*/2, &cfg));
  zx::status result = cfg->get_view();
  ASSERT_TRUE(result.is_ok());
  auto view = std::move(result.value());
  ASSERT_EQ(view.get_size(), PCIE_EXTENDED_CONFIG_SIZE);
  ASSERT_EQ(view.get_offset(), bdf_to_ecam_offset(default_bdf2(), /*start_bus=*/1));
  ASSERT_EQ(view.get_vmo()->get(), ecam_mmio.get_vmo()->get());
}

}  // namespace pci
