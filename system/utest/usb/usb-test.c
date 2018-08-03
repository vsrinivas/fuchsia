// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/device/usb-tester.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define USB_TESTER_DEV_DIR "/dev/class/usb-tester"

static zx_status_t open_test_device(int* out_fd) {
    DIR* d = opendir(USB_TESTER_DEV_DIR);
    if (d == NULL) {
        return ZX_ERR_BAD_STATE;
    }
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        int fd = openat(dirfd(d), de->d_name, O_RDWR);
        if (fd < 0) {
            continue;
        }
        *out_fd = fd;
        closedir(d);
        return ZX_OK;
    }
    closedir(d);
    return ZX_ERR_NOT_FOUND;
}

static bool usb_bulk_loopback_test(void) {
    BEGIN_TEST;

    int dev_fd;
    if (open_test_device(&dev_fd) != ZX_OK) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }
    ASSERT_GE(dev_fd, 0, "invalid device fd");

    usb_tester_params_t params = {
        .data_pattern = USB_TESTER_DATA_PATTERN_CONSTANT,
        .len =  64 * 1024
    };
    ASSERT_EQ(ioctl_usb_tester_bulk_loopback(dev_fd, &params), ZX_OK,
              "bulk loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K");

    params.data_pattern = USB_TESTER_DATA_PATTERN_RANDOM;
    ASSERT_EQ(ioctl_usb_tester_bulk_loopback(dev_fd, &params), ZX_OK,
              "bulk loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K");

    close(dev_fd);
    END_TEST;
}

BEGIN_TEST_CASE(usb_tests)
RUN_TEST(usb_bulk_loopback_test)
END_TEST_CASE(usb_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
