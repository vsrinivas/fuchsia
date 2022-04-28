// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_bus.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_upstream_node.h"
#include "src/devices/bus/drivers/pci/test/fakes/test_device.h"
namespace pci {

// Creates a test device with a given device config using test defaults)

using inspect::InspectTestHelper;
class PciDeviceTests : protected inspect::InspectTestHelper, public zxtest::Test {
 public:
  static constexpr char kTestNodeName[] = "Test";
  FakePciroot& pciroot_proto() { return *pciroot_; }
  ddk::PcirootProtocolClient& pciroot_client() { return *client_; }
  FakeBus& bus() { return bus_; }
  FakeUpstreamNode& upstream() { return upstream_; }
  pci_bdf_t default_bdf() { return default_bdf_; }
  Device& CreateTestDevice(const uint8_t* cfg_buf, size_t cfg_size) {
    // Copy the config dump into a device entry in the ecam.
    memcpy(pciroot_proto().ecam().get(default_bdf()).config, cfg_buf, cfg_size);
    // Create the config object for the device.
    std::unique_ptr<Config> cfg;
    EXPECT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
    // Create and initialize the fake device.
    EXPECT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus(),
                             GetInspectNode(), /*has_acpi=*/false));
    return bus().get_device(default_bdf());
  }

  zx::vmo& inspect_vmo() { return inspect_vmo_; }
  inspect::Inspector& inspector() { return inspector_; }

 protected:
  PciDeviceTests()
      : upstream_(UpstreamNode::Type::ROOT, 0), inspect_vmo_(inspector_.DuplicateVmo()) {}
  void SetUp() override {
    pciroot_ = std::make_unique<FakePciroot>(0, 1);
    client_ = std::make_unique<ddk::PcirootProtocolClient>(pciroot_->proto());
  }
  void TearDown() override {
    upstream_.DisableDownstream();
    upstream_.UnplugDownstream();
  }

  inspect::Node GetInspectNode() { return inspector_.GetRoot().CreateChild(kTestNodeName); }

 private:
  std::unique_ptr<FakePciroot> pciroot_;
  std::unique_ptr<ddk::PcirootProtocolClient> client_;
  FakeBus bus_;
  FakeUpstreamNode upstream_;
  const pci_bdf_t default_bdf_ = {1, 2, 3};
  inspect::Inspector inspector_;
  zx::vmo inspect_vmo_;
};

TEST_F(PciDeviceTests, CreationTest) {
  std::unique_ptr<Config> cfg;

  // This test creates a device, goes through its init sequence, links it into
  // the toplogy, and then has it linger. It will be cleaned up by TearDown()
  // releasing all objects of upstream(). If creation succeeds here and no
  // asserts happen following the test it means the fakes are built properly
  // enough and the basic interface is fulfilled.
  ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  ASSERT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus(),
                           GetInspectNode(), /*has_acpi=*/false));

  // Verify the created device's BDF.
  auto& dev = bus().get_device(default_bdf());
  ASSERT_EQ(default_bdf().bus_id, dev.bus_id());
  ASSERT_EQ(default_bdf().device_id, dev.dev_id());
  ASSERT_EQ(default_bdf().function_id, dev.func_id());
}

// Test a normal capability chain
TEST_F(PciDeviceTests, StdCapabilityTest) {
  std::unique_ptr<Config> cfg;

  // Copy the config dump into a device entry in the ecam.
  memcpy(pciroot_proto().ecam().get(default_bdf()).config, kFakeVirtioInputDeviceConfig.data(),
         kFakeVirtioInputDeviceConfig.max_size());
  ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  ASSERT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus(),
                           GetInspectNode(), /*has_acpi=*/false));
  auto& dev = bus().get_device(default_bdf());

  // Ensure our faked Keyboard exists.
  ASSERT_EQ(0x1af4, dev.vendor_id());
  ASSERT_EQ(0x1052, dev.device_id());

  // Since this is a dump of an emulated device we know it has a single MSI-X
  // capability followed by five Vendor capabilities.
  auto cap_iter = dev.capabilities().list.begin();
  EXPECT_EQ(static_cast<Capability::Id>(cap_iter->id()), Capability::Id::kMsiX);
  ASSERT_TRUE(cap_iter != dev.capabilities().list.end());
  EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
  ASSERT_TRUE(cap_iter != dev.capabilities().list.end());
  EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
  ASSERT_TRUE(cap_iter != dev.capabilities().list.end());
  EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
  ASSERT_TRUE(cap_iter != dev.capabilities().list.end());
  EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
  ASSERT_TRUE(cap_iter != dev.capabilities().list.end());
  EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
  EXPECT_TRUE(++cap_iter == dev.capabilities().list.end());
}

// Test an extended capability chain
TEST_F(PciDeviceTests, ExtendedCapabilityTest) {
  auto& dev = CreateTestDevice(kFakeQuadroDeviceConfig.data(), kFakeQuadroDeviceConfig.max_size());
  ASSERT_EQ(false, CURRENT_TEST_HAS_FAILURES());

  // Since this is a dump of an emulated device we that it should have:
  //
  //      Capabilities: [100] Virtual Channel
  //      Capabilities: [250] Latency Tolerance Reporting
  //      Capabilities: [258] L1 PM Substates
  //      Capabilities: [128] Power Budgeting
  //      Capabilities: [600] Vendor Specific Information
  auto cap_iter = dev.capabilities().ext_list.begin();
  ASSERT_TRUE(cap_iter.IsValid());
  EXPECT_EQ(static_cast<ExtCapability::Id>(cap_iter->id()),
            ExtCapability::Id::kVirtualChannelNoMFVC);
  ASSERT_TRUE(cap_iter != dev.capabilities().ext_list.end());
  EXPECT_EQ(static_cast<ExtCapability::Id>((++cap_iter)->id()),
            ExtCapability::Id::kLatencyToleranceReporting);
  ASSERT_TRUE(cap_iter != dev.capabilities().ext_list.end());
  EXPECT_EQ(static_cast<ExtCapability::Id>((++cap_iter)->id()), ExtCapability::Id::kL1PMSubstates);
  ASSERT_TRUE(cap_iter != dev.capabilities().ext_list.end());
  EXPECT_EQ(static_cast<ExtCapability::Id>((++cap_iter)->id()), ExtCapability::Id::kPowerBudgeting);
  ASSERT_TRUE(cap_iter != dev.capabilities().ext_list.end());
  EXPECT_EQ(static_cast<ExtCapability::Id>((++cap_iter)->id()), ExtCapability::Id::kVendor);
  EXPECT_TRUE(++cap_iter == dev.capabilities().ext_list.end());
}

// This test checks for proper handling of capability pointers that are
// invalid by pointing to inside the config header.
TEST_F(PciDeviceTests, InvalidPtrCapabilityTest) {
  auto& raw_cfg = pciroot_proto().ecam().get(default_bdf()).config;
  auto& fake_dev = pciroot_proto().ecam().get(default_bdf()).device;

  // Two valid locations, followed by a third capability pointing at BAR 1.
  const uint8_t kCap1 = 0x80;
  const uint8_t kCap2 = 0x90;
  const uint8_t kInvalidCap = 0x10;

  // Point to 0x80 as the first capability.
  fake_dev.set_vendor_id(0x8086)
      .set_device_id(0x1234)
      .set_capabilities_list(1)
      .set_capabilities_ptr(kCap1);
  raw_cfg[kCap1] = static_cast<uint8_t>(Capability::Id::kPciPowerManagement);
  raw_cfg[kCap1 + 1] = kCap2;
  raw_cfg[kCap2] = static_cast<uint8_t>(Capability::Id::kMsiX);
  raw_cfg[kCap2 + 1] = kInvalidCap;

  std::unique_ptr<Config> cfg;
  ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(),
                                                &bus(), GetInspectNode(), /*has_acpi=*/false));

  // Ensure no device was added.
  EXPECT_TRUE(bus().devices().is_empty());
}

// This test checks for proper handling (ZX_ERR_BAD_STATE) upon
// funding a pointer cycle while parsing capabilities.
TEST_F(PciDeviceTests, PtrCycleCapabilityTest) {
  // Boilerplate to get a device corresponding to the default_bdf().
  std::unique_ptr<Config> cfg;
  auto& raw_cfg = pciroot_proto().ecam().get(default_bdf()).config;
  auto& fake_dev = pciroot_proto().ecam().get(default_bdf()).device;

  // Two valid locations, followed by a third capability pointing at BAR 1.
  const uint8_t kCap1 = 0x80;
  const uint8_t kCap2 = 0x90;
  const uint8_t kCap3 = 0xA0;

  // Create a Cycle of Cap1 -> Cap2 -> Cap3 -> Cap1
  fake_dev.set_vendor_id(0x8086)
      .set_device_id(0x1234)
      .set_capabilities_list(1)
      .set_capabilities_ptr(kCap1);
  auto cap_id = static_cast<uint8_t>(Capability::Id::kVendor);
  raw_cfg[kCap1] = cap_id;
  raw_cfg[kCap1 + 1] = kCap2;
  raw_cfg[kCap2] = cap_id;
  raw_cfg[kCap2 + 1] = kCap3;
  raw_cfg[kCap3] = cap_id;
  raw_cfg[kCap3 + 1] = kCap1;

  ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  EXPECT_EQ(ZX_ERR_BAD_STATE, Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(),
                                             &bus(), GetInspectNode(), /*has_acpi=*/false));

  // Ensure no device was added.
  EXPECT_TRUE(bus().devices().is_empty());
}

// Test that we properly bail out if we see multiple of a capability
// type that only one should exist of in a system.
TEST_F(PciDeviceTests, DuplicateFixedCapabilityTest) {
  // Boilerplate to get a device corresponding to the default_bdf().
  std::unique_ptr<Config> cfg;
  auto& raw_cfg = pciroot_proto().ecam().get(default_bdf()).config;
  auto& fake_dev = pciroot_proto().ecam().get(default_bdf()).device;

  // Two valid locations, followed by a third capability pointing at BAR 1.
  const uint8_t kCap1 = 0x80;
  const uint8_t kCap2 = 0x90;
  const uint8_t kCap3 = 0xA0;

  // Create a device with three capabilities, two of which are kPciExpress
  fake_dev.set_vendor_id(0x8086)
      .set_device_id(0x1234)
      .set_capabilities_list(1)
      .set_capabilities_ptr(kCap1);
  auto pcie_id = static_cast<uint8_t>(Capability::Id::kPciExpress);
  auto null_id = static_cast<uint8_t>(Capability::Id::kNull);
  raw_cfg[kCap1] = pcie_id;
  raw_cfg[kCap1 + 1] = kCap2;
  raw_cfg[kCap2] = null_id;
  raw_cfg[kCap2 + 1] = kCap3;
  raw_cfg[kCap3] = pcie_id;
  raw_cfg[kCap3 + 1] = 0;

  ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().mmio(), 0, 1, &cfg));
  EXPECT_EQ(ZX_ERR_BAD_STATE, Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(),
                                             &bus(), GetInspectNode(), /*has_acpi=*/false));

  // Ensure no device was added.
  EXPECT_TRUE(bus().devices().is_empty());
}

// Ensure we parse MSI capabilities properly in the Quadro device.
// lspci output: Capabilities: [68] MSI: Enable+ Count=1/4 Maskable- 64bit+
TEST_F(PciDeviceTests, MsiCapabilityTest) {
  auto& dev = CreateTestDevice(kFakeQuadroDeviceConfig.data(), kFakeQuadroDeviceConfig.max_size());
  ASSERT_EQ(false, CURRENT_TEST_HAS_FAILURES());
  ASSERT_NE(nullptr, dev.capabilities().msi);

  auto& msi = *dev.capabilities().msi;
  EXPECT_EQ(0x68, msi.base());
  EXPECT_EQ(static_cast<uint8_t>(Capability::Id::kMsi), msi.id());
  EXPECT_EQ(true, msi.is_64bit());
  EXPECT_EQ(4, msi.vectors_avail());
  EXPECT_EQ(false, msi.supports_pvm());

  // MSI should be disabled by Device initialization.
  MsiControlReg ctrl = {.value = dev.config()->Read(msi.ctrl())};
  EXPECT_EQ(0, ctrl.enable());
}

// Ensure we parse MSIX capabilities properly in the Virtio-input device.
TEST_F(PciDeviceTests, MsixCapabilityTest) {
  auto& dev = CreateTestDevice(kFakeVirtioInputDeviceConfig.data(),
                               kFakeVirtioInputDeviceConfig.max_size());
  ASSERT_EQ(false, CURRENT_TEST_HAS_FAILURES());
  ASSERT_NE(nullptr, dev.capabilities().msix);

  auto& msix = *dev.capabilities().msix;
  EXPECT_EQ(0x98, msix.base());
  EXPECT_EQ(static_cast<uint8_t>(Capability::Id::kMsiX), msix.id());
  EXPECT_EQ(1, msix.table_bar());
  EXPECT_EQ(0, msix.table_offset());
  EXPECT_EQ(2, msix.table_size());
  EXPECT_EQ(1, msix.pba_bar());
  EXPECT_EQ(0x800, msix.pba_offset());

  // MSI-X should be disabled by Device initialization.
  MsixControlReg ctrl = {.value = dev.config()->Read(msix.ctrl())};
  EXPECT_EQ(0, ctrl.enable());
}

TEST_F(PciDeviceTests, InspectIrqMode) {
  auto& dev = CreateTestDevice(kFakeQuadroDeviceConfig.data(), kFakeQuadroDeviceConfig.max_size());
  {
    pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_DISABLED;
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(hierarchy().GetByPath({kTestNodeName})->node(), Device::kInspectIrqMode,
                      inspect::StringPropertyValue(Device::kInspectIrqModes[mode])));
  }
  {
    pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_LEGACY;
    ASSERT_OK(dev.PciSetInterruptMode(mode, 1));
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto* node = hierarchy().GetByPath({kTestNodeName});
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node->node(), Device::kInspectIrqMode,
                      inspect::StringPropertyValue(Device::kInspectIrqModes[mode])));
  }
  {
    pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_LEGACY_NOACK;
    ASSERT_OK(dev.PciSetInterruptMode(mode, 1));
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto* node = hierarchy().GetByPath({kTestNodeName});
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node->node(), Device::kInspectIrqMode,
                      inspect::StringPropertyValue(Device::kInspectIrqModes[mode])));
  }
  {
    pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_MSI;
    ASSERT_OK(dev.PciSetInterruptMode(mode, 1));
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto* node = hierarchy().GetByPath({kTestNodeName});
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node->node(), Device::kInspectIrqMode,
                      inspect::StringPropertyValue(Device::kInspectIrqModes[mode])));
  }

#ifdef ENABLE_MSIX
  {
    pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_MSI_X;
    ASSERT_OK(dev.PciSetInterruptMode(mode, 1));
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto* node = hierarchy().GetByPath({kTestNodeName});
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node->node(), Device::kInspectIrqMode,
                      inspect::StringPropertyValue(Device::kInspectIrqModes[mode])));
  }
#endif
}

TEST_F(PciDeviceTests, InspectLegacyNoPin) {
  auto quadro_copy = kFakeQuadroDeviceConfig;
  quadro_copy[PCI_CONFIG_INTERRUPT_PIN] = 0;
  CreateTestDevice(quadro_copy.data(), quadro_copy.max_size());
  ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
  auto& node = hierarchy().GetByPath({kTestNodeName, Device::kInspectLegacyInterrupt})->node();
  ASSERT_NULL(node.get_property<inspect::StringPropertyValue>(Device::kInspectLegacyInterruptLine));
  ASSERT_NULL(node.get_property<inspect::StringPropertyValue>(Device::kInspectLegacyInterruptPin));
}

TEST_F(PciDeviceTests, InspectLegacy) {
  // Signal and Ack the legacy IRQ once each to ensure add is happening.
  auto& dev = CreateTestDevice(kFakeQuadroDeviceConfig.data(), kFakeQuadroDeviceConfig.max_size());
  ASSERT_OK(dev.PciSetInterruptMode(PCI_INTERRUPT_MODE_LEGACY, 1));
  {
    fbl::AutoLock _(dev.dev_lock());
    ASSERT_OK(dev.SignalLegacyIrq(0x10000));
    ASSERT_OK(dev.AckLegacyIrq());
  }

  // Verify properties in the general case.
  {
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto& node = hierarchy().GetByPath({kTestNodeName, Device::kInspectLegacyInterrupt})->node();
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node, Device::kInspectLegacyInterruptPin, inspect::StringPropertyValue("A")));
    ASSERT_NO_FATAL_FAILURE(CheckProperty(node, Device::kInspectLegacyInterruptLine,
                                          inspect::UintPropertyValue(dev.legacy_vector())));
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node, Device::kInspectLegacyAckCount, inspect::UintPropertyValue(1)));
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node, Device::kInspectLegacySignalCount, inspect::UintPropertyValue(1)));
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node, Device::kInspectLegacyDisabled, inspect::BoolPropertyValue(false)));
  }

  {
    fbl::AutoLock _(dev.dev_lock());
    dev.DisableLegacyIrq();
  }

  {
    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
    auto& node = hierarchy().GetByPath({kTestNodeName, Device::kInspectLegacyInterrupt})->node();
    ASSERT_NO_FATAL_FAILURE(
        CheckProperty(node, Device::kInspectLegacyDisabled, inspect::BoolPropertyValue(true)));
  }
}

#ifdef ENABLE_MSIX
TEST_F(PciDeviceTests, InspectMSI) {
  uint32_t irq_cnt = 4;
  auto& dev = CreateTestDevice(kFakeQuadroDeviceConfig.data(), kFakeQuadroDeviceConfig.max_size());
  ASSERT_OK(dev.PciSetInterruptMode(PCI_INTERRUPT_MODE_MSI_X, irq_cnt));

  zx_info_msi_t info{};
  {
    fbl::AutoLock _(dev.dev_lock());
    dev.msi_allocation().get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr);
  }

  ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
  auto& node = hierarchy().GetByPath({kTestNodeName, Device::kInspectMsi})->node();
  ASSERT_NO_FATAL_FAILURE(CheckProperty(node, Device::kInspectMsiBaseVector,
                                        inspect::UintPropertyValue(info.base_irq_id)));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(node, Device::kInspectMsiAllocated, inspect::UintPropertyValue(irq_cnt)));
}
#endif

}  // namespace pci
