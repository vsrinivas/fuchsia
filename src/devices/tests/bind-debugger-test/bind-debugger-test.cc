// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/device/test/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"

const std::string kDevPrefix = "/dev/";
const std::string kDriverTestDir = "/boot/driver/test";
const std::string kDriverLibname = "bind-debugger-test.so";
const std::string kChildDeviceName = "child";

using devmgr_integration_test::IsolatedDevmgr;

class BindDebuggerTest : public testing::Test {
 protected:
  void SetUp() override {
    auto args = IsolatedDevmgr::DefaultArgs();

    ASSERT_EQ(IsolatedDevmgr::Create(std::move(args), &devmgr_), ZX_OK);
    ASSERT_NE(devmgr_.svc_root_dir().get(), ZX_HANDLE_INVALID);

    // Wait for /dev/test/test to appear, then get a channel to it.
    fbl::unique_fd root_fd;
    zx_status_t status =
        devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), "test/test", &root_fd);
    ASSERT_EQ(status, ZX_OK);
    zx::channel root_channel;
    status = fdio_get_service_handle(root_fd.release(), root_channel.reset_and_get_address());
    ASSERT_EQ(status, ZX_OK);

    // Create the root test device in /dev/test/test, and get its relative path from /dev.
    char device_path[fuchsia_device_test_MAX_DEVICE_PATH_LEN + 1];
    size_t device_path_count;
    zx_status_t call_status;
    status = fuchsia_device_test_RootDeviceCreateDevice(
        root_channel.get(), kDriverLibname.c_str(), kDriverLibname.size(), &call_status,
        device_path, sizeof(device_path) - 1, &device_path_count);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);
    device_path[device_path_count] = 0;

    ASSERT_EQ(strncmp(device_path, kDevPrefix.c_str(), kDevPrefix.size()), 0);
    relative_device_path_ = device_path + kDevPrefix.size();

    // Get a channel to the new device.
    fbl::unique_fd device_fd;
    status = devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), relative_device_path_.c_str(), &device_fd);
    ASSERT_EQ(status, ZX_OK);
    status = fdio_get_service_handle(device_fd.release(), device_channel_.reset_and_get_address());
    ASSERT_EQ(status, ZX_OK);

    // Bind the test driver to the new device.
    driver_libpath_ = kDriverTestDir + "/" + kDriverLibname;
    auto response = ::llcpp::fuchsia::device::Controller::Call::Bind(
        zx::unowned_channel(device_channel_.get()), ::fidl::StringView(driver_libpath_));
    status = response.status();
    if (status == ZX_OK) {
      if (response->result.is_err()) {
        status = response->result.err();
      }
    }
    ASSERT_EQ(status, ZX_OK);

    // Connect to the BindDebugger service.
    zx::channel local, remote;
    ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);

    std::string svc_name =
        fxl::StringPrintf("svc/%s", fuchsia::device::manager::BindDebugger::Name_);
    sys::ServiceDirectory svc_dir(devmgr_.TakeSvcRootDir());
    status = svc_dir.Connect(svc_name, std::move(remote));
    ASSERT_EQ(status, ZX_OK);

    bind_debugger_.Bind(std::move(local));
  }

  void TearDown() override { fuchsia_device_test_DeviceDestroy(device_channel_.get()); }

  IsolatedDevmgr devmgr_;
  zx::channel device_channel_;
  fuchsia::device::manager::BindDebuggerSyncPtr bind_debugger_;
  std::string driver_libpath_;
  std::string relative_device_path_;
};

// Check that calling GetBindProgram with an invalid driver path returns ZX_ERR_NOT_FOUND.
TEST_F(BindDebuggerTest, InvalidDriver) {
  fuchsia::device::manager::BindDebugger_GetBindProgram_Result result;
  ASSERT_EQ(bind_debugger_->GetBindProgram("abc", &result), ZX_OK);
  ASSERT_TRUE(result.is_err());
  ASSERT_EQ(result.err(), ZX_ERR_NOT_FOUND);
}

// Get the bind program of the test driver and check that it has the expected instructions.
TEST_F(BindDebuggerTest, ValidDriver) {
  fuchsia::device::manager::BindDebugger_GetBindProgram_Result result;
  ASSERT_EQ(bind_debugger_->GetBindProgram(driver_libpath_, &result), ZX_OK);
  ASSERT_TRUE(result.is_response());
  auto instructions = result.response().instructions;

  zx_bind_inst_t expected_instructions[] = {BI_ABORT_IF_AUTOBIND,
                                            BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST)};

  ASSERT_EQ(instructions.size(), countof(expected_instructions));
  for (size_t i = 0; i < instructions.size(); i++) {
    ASSERT_EQ(instructions[i].op, expected_instructions[i].op);
    ASSERT_EQ(instructions[i].arg, expected_instructions[i].arg);
  }
}

// Check that calling GetDeviceProperties with an invalid device path returns ZX_ERR_NOT_FOUND.
TEST_F(BindDebuggerTest, InvalidDevice) {
  fuchsia::device::manager::BindDebugger_GetDeviceProperties_Result result;
  ASSERT_EQ(bind_debugger_->GetDeviceProperties("abc", &result), ZX_OK);
  ASSERT_TRUE(result.is_err());
  ASSERT_EQ(result.err(), ZX_ERR_NOT_FOUND);
}

// Get the properties of the test driver's child device and check that they are as expected.
TEST_F(BindDebuggerTest, ValidDevice) {
  std::string child_device_path(relative_device_path_ + "/" + kChildDeviceName);

  fuchsia::device::manager::BindDebugger_GetDeviceProperties_Result result;
  ASSERT_EQ(bind_debugger_->GetDeviceProperties(child_device_path, &result), ZX_OK);

  ASSERT_TRUE(result.is_response());
  auto props = result.response().props;

  zx_device_prop_t expected_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, 1234},
      {BIND_PCI_DID, 0, 1234},
  };

  ASSERT_EQ(props.size(), countof(expected_props));
  for (size_t i = 0; i < props.size(); i++) {
    ASSERT_EQ(props[i].id, expected_props[i].id);
    ASSERT_EQ(props[i].reserved, expected_props[i].reserved);
    ASSERT_EQ(props[i].value, expected_props[i].value);
  }
}
