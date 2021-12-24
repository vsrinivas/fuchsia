// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <gtest/gtest.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"
#include "src/lib/fxl/strings/string_printf.h"

const std::string kDevPrefix = "/dev/";
const std::string kDriverUrl = "fuchsia-boot:///#driver/bind-test-v2-driver.so";
const std::string kDriverLibname = "bind-test-v2-driver.so";
const std::string kChildDeviceName = "child";

class BindCompilerV2Test : public testing::Test {
 protected:
  void SetUp() override {
    // Wait for /dev/sys/test/test to appear, then create an endpoint to it.
    fbl::unique_fd root_fd;
    zx_status_t status = device_watcher::RecursiveWaitForFile("/dev/sys/test/test", &root_fd);
    ASSERT_EQ(status, ZX_OK);

    zx::status root_device_client_end =
        fdio_cpp::FdioCaller(std::move(root_fd)).take_as<fuchsia_device_test::RootDevice>();
    ASSERT_EQ(root_device_client_end.status_value(), ZX_OK);
    auto root_device = fidl::BindSyncClient(std::move(*root_device_client_end));

    auto endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);

    // Create the root test device in /dev/sys/test/test, and get its relative path from /dev.
    auto result = root_device->CreateDevice(fidl::StringView::FromExternal(kDriverLibname),
                                            endpoints->server.TakeChannel());

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->status, ZX_OK);

    ASSERT_GE(result->path.size(), kDevPrefix.size());
    ASSERT_EQ(strncmp(result->path.data(), kDevPrefix.c_str(), kDevPrefix.size()), 0);
    relative_device_path_ = std::string(result->path.data() + kDevPrefix.size(),
                                        result->path.size() - kDevPrefix.size());

    // Bind the test driver to the new device.
    auto response =
        fidl::WireCall(endpoints->client)->Bind(::fidl::StringView::FromExternal(kDriverLibname));
    status = response.status();
    if (status == ZX_OK) {
      if (response->result.is_err()) {
        status = response->result.err();
      }
    }
    ASSERT_EQ(status, ZX_OK);

    // Connect to the DriverDevelopment service.
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(driver_dev_.NewRequest());
  }

  fuchsia::driver::development::DriverDevelopmentSyncPtr driver_dev_;
  std::string relative_device_path_;
};

// Check that calling GetDriverInfo with an invalid driver path returns ZX_ERR_NOT_FOUND.
TEST_F(BindCompilerV2Test, InvalidDriver) {
  fuchsia::driver::development::DriverInfoIteratorSyncPtr iterator;
  ASSERT_EQ(driver_dev_->GetDriverInfo({"abc"}, iterator.NewRequest()), ZX_OK);

  std::vector<fuchsia::driver::development::DriverInfo> drivers;
  ASSERT_NE(iterator->GetNext(&drivers), ZX_OK);
}

// Get the bind program of the test driver and check that it has the expected instructions.
TEST_F(BindCompilerV2Test, ValidDriver) {
  fuchsia::driver::development::DriverInfoIteratorSyncPtr iterator;
  ASSERT_EQ(driver_dev_->GetDriverInfo({kDriverUrl}, iterator.NewRequest()), ZX_OK);

  std::vector<fuchsia::driver::development::DriverInfo> drivers;
  ASSERT_EQ(iterator->GetNext(&drivers), ZX_OK);

  ASSERT_EQ(drivers.size(), 1u);
  auto bytecode = drivers[0].bind_rules().bytecode_v2();

  uint8_t expected_bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x16, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x02, 0x0,  0x0,  0x0, 0x01, 0x0,  0x0, 0x0, 0x0,  // Autobind condition
      0x01, 0x01, 0x01, 0x0,  0x0,  0x0, 0x01, 0x50, 0x0, 0x0, 0x0,  // Device protocol condition
  };

  ASSERT_EQ(std::size(expected_bytecode), bytecode.size());
  for (size_t i = 0; i < bytecode.size(); i++) {
    ASSERT_EQ(expected_bytecode[i], bytecode[i]);
  }
}

// Check that calling GetDeviceInfo with an invalid device path returns ZX_ERR_NOT_FOUND.
TEST_F(BindCompilerV2Test, InvalidDevice) {
  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  ASSERT_EQ(driver_dev_->GetDeviceInfo({"abc"}, iterator.NewRequest()), ZX_OK);
  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  ASSERT_NE(iterator->GetNext(&devices), ZX_OK);
}

// Get the properties of the test driver's child device and check that they are as expected.
TEST_F(BindCompilerV2Test, ValidDevice) {
  std::string child_device_path(relative_device_path_ + "/" + kChildDeviceName);

  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  ASSERT_EQ(driver_dev_->GetDeviceInfo({child_device_path}, iterator.NewRequest()), ZX_OK);

  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  ASSERT_EQ(iterator->GetNext(&devices), ZX_OK);
  ASSERT_EQ(devices.size(), 1u);
  auto props = devices[0].property_list().props;

  zx_device_prop_t expected_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, 1234},
      {BIND_PCI_DID, 0, 1234},
  };

  ASSERT_EQ(props.size(), std::size(expected_props));
  for (size_t i = 0; i < props.size(); i++) {
    ASSERT_EQ(props[i].id, expected_props[i].id);
    ASSERT_EQ(props[i].reserved, expected_props[i].reserved);
    ASSERT_EQ(props[i].value, expected_props[i].value);
  }
}
