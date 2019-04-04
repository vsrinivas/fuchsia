// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/platform-defs.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/hidctl/c/fidl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <hid/boot.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

class HidDriverTest : public zxtest::Test {
    void SetUp() override;

protected:
    IsolatedDevmgr devmgr_;
    fbl::unique_fd hidctl_fd_;
    zx_handle_t hidctl_fdio_channel_;
};

const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "hidctl");
    entry.vid = PDEV_VID_TEST;
    entry.pid = PDEV_PID_HIDCTL_TEST;
    return entry;
}();

void HidDriverTest::SetUp() {
    // Create the isolated dev manager
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.device_list.push_back(kDeviceEntry);
    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_EQ(ZX_OK, status);

    // Wait for HidCtl to be created
    status = devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                           "sys/platform/11:04:0/hidctl",
                                                           zx::deadline_after(zx::sec(5)),
                                                           &hidctl_fd_);
    ASSERT_EQ(ZX_OK, status);

    // Get a FIDL channel to HidCtl
    status = fdio_get_service_handle(hidctl_fd_.get(), &hidctl_fdio_channel_);
    ASSERT_EQ(ZX_OK, status);
}

const uint8_t kBootMouseReportDesc[50] = {
    0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02, // Usage (Mouse)
    0xA1, 0x01, // Collection (Application)
    0x09, 0x01, //   Usage (Pointer)
    0xA1, 0x00, //   Collection (Physical)
    0x05, 0x09, //     Usage Page (Button)
    0x19, 0x01, //     Usage Minimum (0x01)
    0x29, 0x03, //     Usage Maximum (0x03)
    0x15, 0x00, //     Logical Minimum (0)
    0x25, 0x01, //     Logical Maximum (1)
    0x95, 0x03, //     Report Count (3)
    0x75, 0x01, //     Report Size (1)
    0x81, 0x02, //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0x95, 0x01, //     Report Count (1)
    0x75, 0x05, //     Report Size (5)
    0x81, 0x03, //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position)
    0x05, 0x01, //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30, //     Usage (X)
    0x09, 0x31, //     Usage (Y)
    0x15, 0x81, //     Logical Minimum (-127)
    0x25, 0x7F, //     Logical Maximum (127)
    0x75, 0x08, //     Report Size (8)
    0x95, 0x02, //     Report Count (2)
    0x81, 0x06, //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xC0,       //   End Collection
    0xC0,       // End Collection
};

TEST_F(HidDriverTest, BootMouseTest) {
    // Create a fake mouse device
    fuchsia_hardware_hidctl_HidCtlConfig config = {};
    config.dev_num = 5;
    config.boot_device = false;
    config.dev_class = 0;
    zx_handle_t hidctl_channel;
    zx_status_t status = fuchsia_hardware_hidctl_DeviceMakeHidDevice(hidctl_fdio_channel_, &config,
                                                                     kBootMouseReportDesc,
                                                                     sizeof(kBootMouseReportDesc),
                                                                     &hidctl_channel);
    ASSERT_EQ(ZX_OK, status);

    // Open the corresponding /dev/class/input/ device
    fbl::unique_fd fd_device;
    status = devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                           "class/input/000",
                                                           zx::deadline_after(zx::sec(5)),
                                                           &fd_device);
    ASSERT_EQ(ZX_OK, status);

    // Send a single mouse report
    hid_boot_mouse_report_t mouse_report = {};
    mouse_report.rel_x = 50;
    mouse_report.rel_y = 100;
    status = zx_socket_write(hidctl_channel,
                             0,
                             &mouse_report,
                             sizeof(mouse_report),
                             NULL);
    ASSERT_EQ(ZX_OK, status);

    // Check that the report comes through
    hid_boot_mouse_report_t test_report = {};
    size_t bytes_read = read(fd_device.get(), (void*)&test_report, sizeof(test_report));
    ASSERT_EQ(sizeof(test_report), bytes_read);
    ASSERT_EQ(mouse_report.rel_x, test_report.rel_x);
    ASSERT_EQ(mouse_report.rel_y, test_report.rel_y);

    // Open a FIDL channel to the HID device
    zx_handle_t device_fdio_channel;
    status = fdio_get_service_handle(fd_device.get(), &device_fdio_channel);
    ASSERT_EQ(ZX_OK, status);

    // Check that report descriptors have the same length
    uint16_t len;
    status =
        fuchsia_hardware_input_DeviceGetReportDescSize(device_fdio_channel, &len);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(sizeof(kBootMouseReportDesc), len);

    // Check that report descriptors match completely
    uint8_t buf[sizeof(kBootMouseReportDesc)] = {};
    size_t actual;
    status = fuchsia_hardware_input_DeviceGetReportDesc(
        device_fdio_channel, buf, len, &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(sizeof(kBootMouseReportDesc), actual);
    for (size_t i = 0; i < sizeof(kBootMouseReportDesc); i++) {
        if (kBootMouseReportDesc[i] != buf[i]) {
            printf("Index %ld of the report descriptor doesn't match\n", i);
        }
        EXPECT_EQ(kBootMouseReportDesc[i], buf[i]);
    }
}

} // namespace
