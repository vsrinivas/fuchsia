// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/fidl.h"

#include <fuchsia/hardware/acpi/llcpp/fidl.h>

#include <zxtest/zxtest.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"

using acpi::test::Device;
using EvaluateObjectRequestView =
    fidl::WireServer<fuchsia_hardware_acpi::Device>::EvaluateObjectRequestView;
using EvaluateObjectCompleter =
    fidl::WireServer<fuchsia_hardware_acpi::Device>::EvaluateObjectCompleter;

class FidlEvaluateObjectTest : public zxtest::Test {
 public:
  void SetUp() override { acpi_.SetDeviceRoot(std::make_unique<Device>("\\")); }

  void InsertDeviceBelow(std::string path, std::unique_ptr<Device> d) {
    Device* parent = acpi_.GetDeviceRoot()->FindByPath(path);
    ASSERT_NE(parent, nullptr);
    parent->AddChild(std::move(d));
  }

 protected:
  acpi::test::MockAcpi acpi_;
};

TEST_F(FidlEvaluateObjectTest, TestCantEvaluateParent) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(
      &acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0.I2C0"), "\\_SB_.PCI0");

  auto result = helper.ValidateAndLookupPath();
  ASSERT_EQ(result.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestCantEvaluateSibling) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI1")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI1"),
                                        "\\_SB_.PCI0");

  auto result = helper.ValidateAndLookupPath();
  ASSERT_EQ(result.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestCanEvaluateChild) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0"),
                                        "I2C0");

  auto result = helper.ValidateAndLookupPath();
  ASSERT_EQ(result.status_value(), AE_OK);
  ASSERT_EQ(result.value(), "\\_SB_.PCI0.I2C0");
}
