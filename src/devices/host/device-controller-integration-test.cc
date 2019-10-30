// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/test/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <limits.h>

#include <ddk/metadata/test.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;

static constexpr const char kDevPrefix[] = "/dev/";
static constexpr const char kDriverTestDir[] = "/boot/driver/test";
static constexpr const char kPassDriverName[] = "unit-test-pass.so";
static constexpr const char kFailDriverName[] = "unit-test-fail.so";

zx_status_t GetArguments(const fbl::Vector<const char*>& arguments, zx::vmo* out,
                         uint32_t* length) {
  size_t size = 0;
  for (const char* arg : arguments) {
    // Add 1 for the null byte.
    size += strlen(arg) + 1;
  }

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  size_t offset = 0;
  for (const char* arg : arguments) {
    // Add 1 for the null byte.
    size_t length = strlen(arg) + 1;
    status = vmo.write(arg, offset, length);
    if (status != ZX_OK) {
      return status;
    }
    offset += length;
  }

  *length = static_cast<uint32_t>(size);
  *out = std::move(vmo);
  return status;
}

void CreateTestDevice(const IsolatedDevmgr& devmgr, const char* driver_name,
                      zx::channel* dev_channel) {
  fbl::unique_fd root_fd;
  zx_status_t status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &root_fd);
  ASSERT_OK(status);

  zx::channel test_root;
  status = fdio_get_service_handle(root_fd.release(), test_root.reset_and_get_address());
  ASSERT_OK(status);

  char devpath[fuchsia_device_test_MAX_DEVICE_PATH_LEN + 1];
  size_t devpath_count;
  zx_status_t call_status;
  status = fuchsia_device_test_RootDeviceCreateDevice(test_root.get(), driver_name,
                                                      strlen(driver_name), &call_status, devpath,
                                                      sizeof(devpath) - 1, &devpath_count);
  ASSERT_OK(status);
  ASSERT_OK(call_status);
  devpath[devpath_count] = 0;
  ASSERT_STR_NE(devpath, kDevPrefix);

  const char* relative_devpath = devpath + strlen(kDevPrefix);
  fbl::unique_fd fd;
  status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), relative_devpath, &fd);
  ASSERT_OK(status);

  status = fdio_get_service_handle(fd.release(), dev_channel->reset_and_get_address());
  ASSERT_OK(status);
}

// Test binding second time
TEST(DeviceControllerIntegrationTest, TestDuplicateBindSameDriver) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_OK(call_status);

  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_EQ(call_status, ZX_ERR_ALREADY_BOUND);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

// Test binding again, but with different driver
TEST(DeviceControllerIntegrationTest, TestDuplicateBindDifferentDriver) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_OK(call_status);

  len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_EQ(call_status, ZX_ERR_ALREADY_BOUND);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

TEST(DeviceControllerIntegrationTest, AllTestsEnabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  fbl::Vector<const char*> arguments;
  arguments.push_back("driver.tests.enable=true");
  args.get_arguments = [&arguments](zx::vmo* out, uint32_t* length) {
    return GetArguments(arguments, out, length);
  };

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_OK(call_status);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

TEST(DeviceControllerIntegrationTest, AllTestsEnabledBindFail) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  fbl::Vector<const char*> arguments;
  arguments.push_back("driver.tests.enable=true");
  args.get_arguments = [&arguments](zx::vmo* out, uint32_t* length) {
    return GetArguments(arguments, out, length);
  };

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_ERR_BAD_STATE, call_status);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

// Test the flag using bind failure as a proxy for "the unit test did run".
TEST(DeviceControllerIntegrationTest, SpecificTestEnabledBindFail) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  fbl::Vector<const char*> arguments;
  arguments.push_back("driver.unit_test_fail.tests.enable=true");
  args.get_arguments = [&arguments](zx::vmo* out, uint32_t* length) {
    return GetArguments(arguments, out, length);
  };

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_ERR_BAD_STATE, call_status);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

// Test the flag using bind success as a proxy for "the unit test didn't run".
TEST(DeviceControllerIntegrationTest, DefaultTestsDisabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_OK(call_status);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

// Test the flag using bind success as a proxy for "the unit test didn't run".
TEST(DeviceControllerIntegrationTest, SpecificTestDisabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  fbl::Vector<const char*> arguments;
  arguments.push_back("driver.tests.enable=true");
  arguments.push_back("driver.unit_test_fail.tests.enable=false");
  args.get_arguments = [&arguments](zx::vmo* out, uint32_t* length) {
    return GetArguments(arguments, out, length);
  };

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status;
  status = fuchsia_device_ControllerBind(dev_channel.get(), libpath, len, &call_status);
  ASSERT_OK(status);
  ASSERT_OK(call_status);

  fuchsia_device_test_DeviceDestroy(dev_channel.get());
}

}  // namespace
