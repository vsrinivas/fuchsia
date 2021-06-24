// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/manager.h"

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <initializer_list>
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

static void ExpectProps(acpi::DeviceBuilder* b, std::vector<zx_device_prop_t> expected_dev,
                        std::vector<zx_device_str_prop_t> expected_str) {
  auto dev_props = b->GetDevProps();
  zxlogf(INFO, "size: %lu", dev_props.size());
  size_t left = expected_dev.size();
  for (auto& expected_prop : expected_dev) {
    for (auto& actual : dev_props) {
      if (actual.id == expected_prop.id) {
        zxlogf(INFO, "%s 0x%x 0x%x vs 0x%x 0x%x", b->name(), actual.id, actual.value,
               expected_prop.id, expected_prop.value);
        ASSERT_EQ(actual.value, expected_prop.value);
        left--;
      }
    }
  }
  ASSERT_EQ(left, 0);

  auto& str_props = b->GetStrProps();
  left = expected_str.size();

  for (auto& expected_prop : expected_str) {
    for (auto& actual : str_props) {
      if (!strcmp(actual.key, expected_prop.key)) {
        ASSERT_EQ(actual.property_value.value_type, expected_prop.property_value.value_type);
        switch (actual.property_value.value_type) {
          case ZX_DEVICE_PROPERTY_VALUE_INT:
            ASSERT_EQ(actual.property_value.value.int_val,
                      expected_prop.property_value.value.int_val);
            break;
          case ZX_DEVICE_PROPERTY_VALUE_STRING:
            ASSERT_STR_EQ(actual.property_value.value.str_val,
                          expected_prop.property_value.value.str_val);
            break;
          case ZX_DEVICE_PROPERTY_VALUE_BOOL:
            ASSERT_EQ(actual.property_value.value.bool_val,
                      expected_prop.property_value.value.bool_val);
            break;
          default:
            // This should never happen.
            ASSERT_TRUE(false);
            break;
        }
        left--;
      }
    }
    ASSERT_EQ(left, 0);
  }
}

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

TEST_F(AcpiManagerTest, TestDeviceWithHidCid) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  auto device = std::make_unique<Device>("TEST");
  device->SetHid("GGGG0000");
  device->SetCids({"AAAA1111"});

  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::move(device)));
  ASSERT_NO_FATAL_FAILURES(DiscoverConfigurePublish());
  Device* test = acpi_.GetDeviceRoot()->FindByPath("\\_SB_.TEST");
  // Check the properties were generated as expected.
  acpi::DeviceBuilder* builder = manager_.LookupDevice(test);
  ASSERT_NO_FATAL_FAILURES(ExpectProps(
      builder,
      {
          zx_device_prop_t{.id = BIND_ACPI_HID_0_3, .value = 0x47474747},
          zx_device_prop_t{.id = BIND_ACPI_HID_4_7, .value = 0x30303030},
          zx_device_prop_t{.id = BIND_ACPI_CID_0_3, .value = 0x41414141},
          zx_device_prop_t{.id = BIND_ACPI_CID_4_7, .value = 0x31313131},
      },
      {
          zx_device_str_prop_t{.key = "acpi.hid", .property_value = str_prop_str_val("GGGG0000")},
      }));
}
