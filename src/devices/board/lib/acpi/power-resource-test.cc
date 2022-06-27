// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/power-resource.h"

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/test/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "third_party/acpica/source/include/actypes.h"
#include "zxtest/base/values.h"

using acpi::test::Device;

class AcpiPowerResourceTest : public zxtest::Test {
 public:
  AcpiPowerResourceTest() = default;
  void SetUp() override { acpi_.SetDeviceRoot(std::make_unique<Device>("\\")); }

 protected:
  acpi::test::MockAcpi acpi_;
};

TEST_F(AcpiPowerResourceTest, TestPowerResource) {
  auto device = std::make_unique<Device>("PRIC");
  device->SetPowerResourceMethods(3, 5);
  ACPI_HANDLE power_resource_handle = device.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(device));
  acpi::PowerResource power_resource(&acpi_, power_resource_handle);

  Device* mock_power_resource = acpi_.GetDeviceRoot()->FindByPath("\\PRIC");

  ASSERT_OK(power_resource.Init());
  ASSERT_EQ(power_resource.system_level(), 3);
  ASSERT_EQ(power_resource.resource_order(), 5);

  ASSERT_OK(power_resource.Reference());
  ASSERT_EQ(mock_power_resource->sta(), 1);
  ASSERT_TRUE(power_resource.is_on());
  ASSERT_OK(power_resource.Dereference());
  ASSERT_EQ(mock_power_resource->sta(), 0);
  ASSERT_FALSE(power_resource.is_on());

  // Check power resource stays on until ref count reaches zero.
  for (int i = 0; i < 3; ++i) {
    ASSERT_OK(power_resource.Reference());
    ASSERT_EQ(mock_power_resource->sta(), 1);
    ASSERT_TRUE(power_resource.is_on());
  }

  for (int i = 0; i < 2; ++i) {
    ASSERT_OK(power_resource.Dereference());
    ASSERT_EQ(mock_power_resource->sta(), 1);
    ASSERT_TRUE(power_resource.is_on());
  }

  ASSERT_OK(power_resource.Dereference());
  ASSERT_EQ(mock_power_resource->sta(), 0);
  ASSERT_FALSE(power_resource.is_on());
}
