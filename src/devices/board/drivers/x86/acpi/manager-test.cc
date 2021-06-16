// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/manager.h"

#include <lib/ddk/debug.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <map>

#include <zxtest/zxtest.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"

using acpi::test::Device;

class AcpiFakeBind : public fake_ddk::Bind {
 public:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    // Because of the way fake_ddk is implemented, it's not trivial to verify that the proper
    // "tree" structure is replicated.
    // We just substitute kFakeDevice for kFakeParent so that the fake_ddk implementation doesn't
    // complain.
    if (parent == fake_ddk::kFakeDevice) {
      parent = fake_ddk::kFakeParent;
    }

    // DdkAdd() sets ctx to be "this".
    added_devices.emplace_back(static_cast<acpi::Device*>(args->ctx));
    return fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
  }

  std::vector<std::unique_ptr<acpi::Device>> added_devices;
};

class AcpiManagerTest : public zxtest::Test {
 public:
  AcpiManagerTest() : manager_(&acpi_, fake_ddk::kFakeParent) {}
  void SetUp() override { acpi_.SetDeviceRoot(std::make_unique<Device>("\\")); }

  void InsertDeviceBelow(std::string path, std::unique_ptr<Device> d) {
    Device* parent = acpi_.GetDeviceRoot()->FindByPath(path);
    ASSERT_NE(parent, nullptr);
    parent->AddChild(std::move(d));
  }

  void DiscoverConfigurePublish() {
    auto ret = manager_.DiscoverDevices();
    ASSERT_TRUE(ret.is_ok());

    ret = manager_.ConfigureDiscoveredDevices();
    ASSERT_TRUE(ret.is_ok());

    ret = manager_.PublishDevices(fake_ddk::kFakeParent);
    ASSERT_TRUE(ret.is_ok());
  }

 protected:
  AcpiFakeBind fake_ddk_;
  acpi::test::MockAcpi acpi_;
  acpi::Manager manager_;
};

TEST_F(AcpiManagerTest, TestEnumerateEmptyTables) {
  ASSERT_NO_FATAL_FAILURES(DiscoverConfigurePublish());
  ASSERT_EQ(fake_ddk_.added_devices.size(), 0);
}

TEST_F(AcpiManagerTest, TestEnumerateSystemBus) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(DiscoverConfigurePublish());

  ASSERT_EQ(fake_ddk_.added_devices.size(), 1);
}

TEST_F(AcpiManagerTest, TestDevicesOnPciBus) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  auto device = std::make_unique<Device>("PCI0");
  device->SetHid("PNP0A08");
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::move(device)));

  device = std::make_unique<Device>("TEST");
  device->SetAdr(0x00010002);
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::move(device)));

  ASSERT_NO_FATAL_FAILURES(DiscoverConfigurePublish());
  ASSERT_EQ(fake_ddk_.added_devices.size(), 3);

  Device* pci_bus = acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0");
  // Check the PCI bus's type was set correctly.
  acpi::DeviceBuilder* builder = manager_.LookupDevice(pci_bus);
  ASSERT_EQ(builder->GetBusType(), acpi::BusType::kPci);
}
