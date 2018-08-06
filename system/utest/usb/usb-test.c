// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/device/usb-tester.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define USB_TESTER_DEV_DIR "/dev/class/usb-tester"

#define ISOCH_MIN_PASS_PERCENT 80
#define ISOCH_MIN_PACKETS      10lu

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

static bool usb_isoch_verify_result(usb_tester_params_t* params, usb_tester_result_t* result) {
    BEGIN_HELPER;

    ASSERT_GT(result->num_packets, 0lu, "didn't transfer any isochronous packets");
    // Isochronous transfers aren't guaranteed, so just require a high enough percentage to pass.
    ASSERT_GE(result->num_packets, ISOCH_MIN_PACKETS,
              "num_packets is too low for a reliable result, should request more bytes");
    double percent_passed = ((double)result->num_passed / result->num_packets) * 100;
    ASSERT_GE(percent_passed, ISOCH_MIN_PASS_PERCENT, "not enough isoch transfers succeeded");

    END_HELPER;
}

static bool usb_isoch_loopback_test(void) {
    BEGIN_TEST;

    int dev_fd;
    if (open_test_device(&dev_fd) != ZX_OK) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }
    ASSERT_GE(dev_fd, 0, "Invalid device fd");

    usb_tester_params_t params = {
        .data_pattern = USB_TESTER_DATA_PATTERN_CONSTANT,
        .len =  64 * 1024
    };
    char err_msg1[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K";
    usb_tester_result_t result = {};
    ASSERT_EQ(ioctl_usb_tester_isoch_loopback(dev_fd, &params, &result),
              (ssize_t)(sizeof(result)), err_msg1);
    ASSERT_TRUE(usb_isoch_verify_result(&params, &result), err_msg1);

    char err_msg2[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K";
    params.data_pattern = USB_TESTER_DATA_PATTERN_RANDOM;
    ASSERT_EQ(ioctl_usb_tester_isoch_loopback(dev_fd, &params, &result),
              (ssize_t)(sizeof(result)), err_msg2);
    ASSERT_TRUE(usb_isoch_verify_result(&params, &result), err_msg2);

    close(dev_fd);

    END_TEST;
}

BEGIN_TEST_CASE(usb_tests)
RUN_TEST(usb_bulk_loopback_test)
RUN_TEST(usb_isoch_loopback_test)
END_TEST_CASE(usb_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
