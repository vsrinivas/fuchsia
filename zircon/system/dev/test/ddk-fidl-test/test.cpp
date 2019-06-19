// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/device/manager/test/c/fidl.h>
#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

namespace fuchsia = ::llcpp::fuchsia;

const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "ddk-fidl");
    entry.vid = PDEV_VID_TEST;
    entry.pid = PDEV_PID_DDKFIDL_TEST;
    entry.did = PDEV_DID_TEST_DDKFIDL;
    return entry;
}();

TEST(FidlDDKDispatcherTest, TransactionTest) {
    IsolatedDevmgr devmgr;
    zx_handle_t driver_channel;

    // Set the driver arguments.
    IsolatedDevmgr::Args args;
    args.device_list.push_back(kDeviceEntry);

    args.load_drivers.push_back("/boot/driver/fidl-llcpp-driver.so");
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);

    // Create the isolated Devmgr.
    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
    ASSERT_EQ(ZX_OK, status);

    // Wait for the driver to be created
    fbl::unique_fd fd;
    status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                           "sys/platform/11:09:d/ddk-fidl",
                                                           &fd);
    ASSERT_EQ(ZX_OK, status);

    // Get a FIDL channel to the device
    status = fdio_get_service_handle(fd.get(), &driver_channel);
    ASSERT_EQ(ZX_OK, status);

    fuchsia::hardware::serial::Class device_class;
    status = fuchsia::hardware::serial::Device::Call::GetClass(
        zx::unowned_channel(driver_channel), &device_class);
    ASSERT_EQ(ZX_OK, status);

    // Confirm the result of the call
    ASSERT_EQ(fuchsia::hardware::serial::Class::CONSOLE, device_class);
}
} // namespace
