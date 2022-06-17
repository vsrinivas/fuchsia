// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/power-resource.h"

#include <zxtest/zxtest.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/test/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

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

  device->AddMethodCallback(std::nullopt, [](std::optional<std::vector<ACPI_OBJECT>>) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->PowerResource.Type = ACPI_TYPE_POWER;
    retval->PowerResource.SystemLevel = 3;
    retval->PowerResource.ResourceOrder = 5;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  ACPI_HANDLE power_resource_handle = device.get();

  acpi_.GetDeviceRoot()->AddChild(std::move(device));

  acpi::PowerResource power_resource(&acpi_, power_resource_handle);

  ASSERT_OK(power_resource.Init());
  ASSERT_EQ(power_resource.system_level(), 3);
  ASSERT_EQ(power_resource.resource_order(), 5);
}
