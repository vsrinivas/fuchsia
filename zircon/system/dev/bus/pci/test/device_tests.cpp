// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "../config.h"
#include "../device.h"
#include "fake_bus.h"
#include "fake_pciroot.h"
#include "fake_upstream_node.h"
#include <ddktl/protocol/pciroot.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <zircon/limits.h>
#include <zxtest/zxtest.h>

namespace pci {

class PciDeviceTests : public zxtest::Test {
public:
    FakePciroot& pciroot_proto() { return *pciroot_; }
    ddk::PcirootProtocolClient& pciroot_client() { return *client_; }
    FakeBus& bus() { return bus_; }
    FakeUpstreamNode& upstream() { return upstream_; }
    const pci_bdf_t default_bdf() { return default_bdf_; }

protected:
    PciDeviceTests()
        : upstream_(UpstreamNode::Type::ROOT, 0) {}
    void SetUp() {
        ASSERT_EQ(ZX_OK, FakePciroot::Create(0, 1, &pciroot_));
        client_ = std::make_unique<ddk::PcirootProtocolClient>(pciroot_->proto());
    }
    void TearDown() {
        upstream_.DisableDownstream();
        upstream_.UnplugDownstream();
    }

private:
    std::unique_ptr<FakePciroot> pciroot_;
    std::unique_ptr<ddk::PcirootProtocolClient> client_;
    FakeBus bus_;
    FakeUpstreamNode upstream_;
    const pci_bdf_t default_bdf_ = {1, 2, 3};
};

TEST_F(PciDeviceTests, CreationTest) {
    fbl::RefPtr<Config> cfg;

    // This test creates a device, goes through its init sequence, links it into
    // the toplogy, and then has it linger. It will be cleaned up by TearDown()
    // releasing all objects of upstream(). If creation succeeds here and no
    // asserts happen following the test it means the fakes are built properly
    // enough and the basic interface is fulfilled.
    ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    ASSERT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));

    // Verify the created device's BDF.
    auto& dev = bus().get_device(default_bdf());
    ASSERT_EQ(default_bdf().bus_id, dev.bus_id());
    ASSERT_EQ(default_bdf().device_id, dev.dev_id());
    ASSERT_EQ(default_bdf().function_id, dev.func_id());
}

// Test a normal capability chain
TEST_F(PciDeviceTests, BasicCapabilityTest) {
    // This is the configuration space dump of a virtio-input device. It should
    // contain an MSIX capability along with 5 Vendor capabilities.
    uint8_t virtio_input[] = {
        0xf4, 0x1a, 0x52, 0x10, 0x07, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xbf, 0xfe,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0xc0, 0x00, 0xfe,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x1a, 0x00, 0x11,
        0x00, 0x00, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0a, 0x01, 0x00, 0x00, 0x09, 0x00, 0x10, 0x01, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x09, 0x40, 0x10, 0x03,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        0x09, 0x50, 0x10, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x09, 0x60, 0x14, 0x02, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x30, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x09, 0x70, 0x14, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x84, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    static_assert(sizeof(virtio_input) == 256);
    fbl::RefPtr<Config> cfg;

    // Copy the config dump into a device entry in the ecam.
    memcpy(pciroot_proto().ecam().get(default_bdf()).config, virtio_input, sizeof(virtio_input));
    ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    ASSERT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));
    auto& dev = bus().get_device(default_bdf());

    // Ensure our faked Keyboard exists.
    ASSERT_EQ(0x1af4, dev.vendor_id());
    ASSERT_EQ(0x1052, dev.device_id());

    // Since this is a dump of an emulated device we know it has a single MSI-X
    // capability followed by five Vendor capabilities.
    auto cap_iter = dev.capabilities().begin();
    EXPECT_EQ(static_cast<Capability::Id>(cap_iter->id()), Capability::Id::kMsiX);
    ASSERT_TRUE(cap_iter != dev.capabilities().end());
    EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
    ASSERT_TRUE(cap_iter != dev.capabilities().end());
    EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
    ASSERT_TRUE(cap_iter != dev.capabilities().end());
    EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
    ASSERT_TRUE(cap_iter != dev.capabilities().end());
    EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
    ASSERT_TRUE(cap_iter != dev.capabilities().end());
    EXPECT_EQ(static_cast<Capability::Id>((++cap_iter)->id()), Capability::Id::kVendor);
    EXPECT_TRUE(++cap_iter == dev.capabilities().end());
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

    fbl::RefPtr<Config> cfg;
    ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
              Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));

    // Ensure no device was added.
    EXPECT_TRUE(bus().device_list().is_empty());
}

// This test checks for proper handling (ZX_ERR_BAD_STATE) upon
// funding a pointer cycle while parsing capabilities.
TEST_F(PciDeviceTests, PtrCycleCapabilityTest) {
    // Boilerplate to get a device corresponding to the default_bdf().
    fbl::RefPtr<Config> cfg;
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

    ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    EXPECT_EQ(ZX_ERR_BAD_STATE,
              Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));

    // Ensure no device was added.
    EXPECT_TRUE(bus().device_list().is_empty());
}

// Test that we properly bail out if we see multiple of a capability
// type that only one should exist of in a system.
TEST_F(PciDeviceTests, DuplicateFixedCapabilityTest) {
    // Boilerplate to get a device corresponding to the default_bdf().
    fbl::RefPtr<Config> cfg;
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

    ASSERT_OK(MmioConfig::Create(default_bdf(), &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    EXPECT_EQ(ZX_ERR_BAD_STATE,
              Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));

    // Ensure no device was added.
    EXPECT_TRUE(bus().device_list().is_empty());
}

} // namespace pci
