// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_test_base.h>
#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <lib/inspect/cpp/hierarchy.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace chromiumos_ec_core {

using fuchsia_hardware_google_ec::wire::EcStatus;
using inspect::InspectTestHelper;

class ChromiumosEcCoreTest : public ChromiumosEcTestBase {
 public:
  template <typename T>
  void WaitForCoreProperty(const char* prop_name) {
    bool found = false;
    // The driver populates inspect properties asynchronously, so poll until we see it get set.
    while (!found) {
      ASSERT_NO_FATAL_FAILURE(ReadInspect(device_->inspect()));
      found = hierarchy().GetByPath({kNodeCore})->node().get_property<T>(prop_name);
    }
  }

  void WaitAndCheckProperty(const char* prop_name, const char* value) {
    WaitForCoreProperty<inspect::StringPropertyValue>(prop_name);
    auto& node = hierarchy().GetByPath({kNodeCore})->node();
    ASSERT_NO_FATAL_FAILURE(CheckProperty(node, prop_name, inspect::StringPropertyValue(value)));
  }

  void WaitAndCheckProperty(const char* prop_name, unsigned int value) {
    WaitForCoreProperty<inspect::UintPropertyValue>(prop_name);
    auto& node = hierarchy().GetByPath({kNodeCore})->node();
    ASSERT_NO_FATAL_FAILURE(CheckProperty(node, prop_name, inspect::UintPropertyValue(value)));
  }
};

TEST_F(ChromiumosEcCoreTest, LifetimeTest) { InitDevice(); }

// Test inspect attributes populated by EC_CMD_GET_VERSION.
TEST_F(ChromiumosEcCoreTest, TestGetVersionInspect) {
  fake_ec_.AddCommand(
      EC_CMD_GET_VERSION, 0,
      [](const void* data, size_t data_size, FakeEcDevice::RunCommandCompleter::Sync& completer) {
        ec_response_get_version response{
            .version_string_ro = "A read-only version string",
            .version_string_rw = "A read-write version string",
            .reserved = {0},
            .current_image = 2,
        };

        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
      });
  InitDevice();

  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropVersionRo, "A read-only version string"));
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropVersionRw, "A read-write version string"));
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropCurrentImage, 2));
}

// Test inspect attributes populated by EC_CMD_GET_BUILD_INFO.
TEST_F(ChromiumosEcCoreTest, TestBuildInfoInspect) {
  fake_ec_.AddCommand(
      EC_CMD_GET_BUILD_INFO, 0,
      [](const void* data, size_t data_size, FakeEcDevice::RunCommandCompleter::Sync& completer) {
        char response[] = "Build info for the EC";
        completer.ReplySuccess(EcStatus::kSuccess,
                               fidl::VectorView<uint8_t>::FromExternal(
                                   reinterpret_cast<uint8_t*>(response), std::size(response) - 1));
      });

  InitDevice();
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropBuildInfo, "Build info for the EC"));
}

// Test inspect attributes populated by EC_CMD_GET_CHIP_INFO.
TEST_F(ChromiumosEcCoreTest, TestGetChipInfoInspect) {
  fake_ec_.AddCommand(
      EC_CMD_GET_CHIP_INFO, 0,
      [](const void* data, size_t data_size, FakeEcDevice::RunCommandCompleter::Sync& completer) {
        ec_response_get_chip_info response{
            .vendor = "ACME Corp",
            .name = "Foobar",
            .revision = "2B",
        };

        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
      });
  InitDevice();

  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropChipVendor, "ACME Corp"));
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropChipName, "Foobar"));
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropChipRevision, "2B"));
}

// Test inspect attributes populated by EC_CMD_GET_BOARD_VERSION
TEST_F(ChromiumosEcCoreTest, TestBoardVersionInspect) {
  fake_ec_.AddCommand(
      EC_CMD_GET_BOARD_VERSION, 0,
      [](const void* data, size_t data_size, FakeEcDevice::RunCommandCompleter::Sync& completer) {
        ec_response_board_version response{
            .board_version = 65,
        };
        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
      });

  InitDevice();
  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropBoardVersion, 65));
}

// Test feature inspect attributes.
TEST_F(ChromiumosEcCoreTest, TestFeaturesInspect) {
  fake_ec_.SetFeatures({EC_FEATURE_LIMITED, EC_FEATURE_GPIO});
  InitDevice();

  ASSERT_NO_FATAL_FAILURE(WaitAndCheckProperty(kPropFeatures, "LIMITED, GPIO"));
}
}  // namespace chromiumos_ec_core
