// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <gtest/gtest.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"
#include "src/lib/fxl/strings/string_printf.h"

const std::string kDriverBaseUrl = "fuchsia-boot:///#driver";
const std::string kStringBindDriverLibPath = kDriverBaseUrl + "/string-bind-child.so";
const std::string kChildDevicePath = "sys/test/parent";

class StringBindTest : public testing::Test {
 protected:
  void SetUp() override {
    // Wait for the child device to bind and appear. The child device should bind with its string
    // properties.
    fbl::unique_fd string_bind_fd;
    zx_status_t status =
        device_watcher::RecursiveWaitForFile("/dev/sys/test/parent/child", &string_bind_fd);
    ASSERT_EQ(ZX_OK, status);

    // Connect to the DriverDevelopment service.
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(driver_dev_.NewRequest());
  }

  fuchsia::driver::development::DriverDevelopmentSyncPtr driver_dev_;
};

// Get the bind program of the test driver and check that it has the expected instructions.
TEST_F(StringBindTest, DriverBytecode) {
  fuchsia::driver::development::DriverInfoIteratorSyncPtr iterator;
  ASSERT_EQ(ZX_OK, driver_dev_->GetDriverInfo({kStringBindDriverLibPath}, iterator.NewRequest()));

  std::vector<fuchsia::driver::development::DriverInfo> drivers;
  ASSERT_EQ(iterator->GetNext(&drivers), ZX_OK);
  ASSERT_EQ(drivers.size(), 1u);
  auto bytecode = drivers[0].bind_rules().bytecode_v2();

  const uint8_t kExpectedBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0,  0x0,  0x0,               // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x45, 0x00, 0x00, 0x00,              // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                       // "stringbind.lib.kinglet" ID
      0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x62, 0x69, 0x6e, 0x64,  // "stringbind"
      0x2e, 0x6c, 0x69, 0x62, 0x2e, 0x6b, 0x69, 0x6e, 0x67, 0x6c,  // ".lib.kingl"
      0x65, 0x74, 0x00,                                            // "et"
      0x02, 0x00, 0x00, 0x00,                                      // "firecrest" ID
      0x66, 0x69, 0x72, 0x65, 0x63, 0x72, 0x65, 0x73, 0x74, 0x00,  // "firecrest"
      0x03, 0x00, 0x00, 0x00,                                      // "stringbind.lib.bobolink" ID
      0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x62, 0x69, 0x6e, 0x64,  // "stringbind"
      0x2e, 0x6c, 0x69, 0x62, 0x2e, 0x62, 0x6f, 0x62, 0x6f, 0x6c,  // ".lib.bobol"
      0x69, 0x6e, 0x6b, 0x00,                                      // "ink"
      0x49, 0x4E, 0x53, 0x54, 0x21, 0x00, 0x00, 0x00,              // Instruction header
      0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x00,
  };

  ASSERT_EQ(countof(kExpectedBytecode), bytecode.size());
  for (size_t i = 0; i < bytecode.size(); i++) {
    ASSERT_EQ(kExpectedBytecode[i], bytecode[i]);
  }
}

TEST_F(StringBindTest, DeviceProperties) {
  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  ASSERT_EQ(ZX_OK, driver_dev_->GetDeviceInfo({kChildDevicePath}, iterator.NewRequest()));

  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  ASSERT_EQ(iterator->GetNext(&devices), ZX_OK);

  constexpr zx_device_prop_t kExpectedProps[] = {
      {BIND_PROTOCOL, 0, 3},
      {BIND_PCI_VID, 0, 1234},
      {BIND_PCI_DID, 0, 1234},
  };

  ASSERT_EQ(devices.size(), 1u);
  auto props = devices[0].property_list().props;
  ASSERT_EQ(props.size(), countof(kExpectedProps));
  for (size_t i = 0; i < props.size(); i++) {
    ASSERT_EQ(props[i].id, kExpectedProps[i].id);
    ASSERT_EQ(props[i].reserved, kExpectedProps[i].reserved);
    ASSERT_EQ(props[i].value, kExpectedProps[i].value);
  }

  auto& str_props = devices[0].property_list().str_props;
  ASSERT_EQ(static_cast<size_t>(2), str_props.size());

  ASSERT_STREQ("stringbind.lib.kinglet", str_props[0].key.data());
  ASSERT_TRUE(str_props[0].value.is_str_value());
  ASSERT_STREQ("firecrest", str_props[0].value.str_value().data());

  ASSERT_STREQ("stringbind.lib.bobolink", str_props[1].key.data());
  ASSERT_TRUE(str_props[1].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(10), str_props[1].value.int_value());
}
