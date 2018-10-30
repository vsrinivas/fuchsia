// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/util.h>
#include <unittest/unittest.h>
#include <zircon/usb/tester/c/fidl.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define USB_TESTER_DEV_DIR "/dev/class/usb-tester"
#define USB_DEVICE_DEV_DIR "/dev/class/usb-device"

#define ISOCH_MIN_PASS_PERCENT 80
#define ISOCH_MIN_PACKETS      10lu

static DIR* open_usb_device_dir(void) {
    return opendir(USB_DEVICE_DEV_DIR);
}

static zx_status_t check_xhci_root_hubs(DIR* d) {
    struct dirent* de;
    uint8_t count = 0;
    while ((de = readdir(d)) != NULL) {
        count++;
    }
    //TODO(ravoorir): Use FIDL apis to read the descriptors
    //of the devices and ensure that both 2.0 root hub and
    //3.0 root hub showed up.
    if (count < 2) {
        return ZX_ERR_BAD_STATE;
    }
    closedir(d);
    return ZX_OK;
}

static bool usb_root_hubs_test(void) {
    BEGIN_TEST;
    //TODO(ravoorir): Wait for /dev/class/usb
    //to be created.
    DIR* d = open_usb_device_dir();
    if (d == NULL) {
        //TODO(ravoorir): At the moment we cannot restrict a test
        //to only run on hardware(IN-497) and not the qemu instances.
        //We should fail here when running on hardware.
        unittest_printf_critical(" Root hub creation failed.[SKIPPING]");
        return true;
    }
    //TODO(ravoorir): There should be a matrix of hardware that should
    //be accessible from here. Depending on whether the hardware has
    //xhci/ehci, we should check the root hubs.
    zx_status_t status = check_xhci_root_hubs(d);
    if (status != ZX_OK) {
        unittest_printf_critical(" Root hub creation failed.[SKIPPING]");
        return true;
    }
    END_TEST;
}

static zx_status_t open_test_device(zx_handle_t* out_svc) {
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
        zx_handle_t svc;
        zx_status_t status = fdio_get_service_handle(fd, &svc);
        if (status != ZX_OK) {
            continue;
        }
        *out_svc = svc;
        closedir(d);
        return ZX_OK;
    }
    closedir(d);
    return ZX_ERR_NOT_FOUND;
}

static bool usb_bulk_loopback_test(void) {
    BEGIN_TEST;

    zx_handle_t dev_svc;
    if (open_test_device(&dev_svc) != ZX_OK) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }
    ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "invalid device service handle");

    zircon_usb_tester_TestParams params = {
        .data_pattern = zircon_usb_tester_DataPatternType_CONSTANT,
        .len =  64 * 1024
    };
    zx_status_t status;
    ASSERT_EQ(zircon_usb_tester_DeviceBulkLoopback(dev_svc, &params, &status), ZX_OK,
              "failed to call DeviceBulkLoopback");
    ASSERT_EQ(status, ZX_OK, "bulk loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K");

    params.data_pattern = zircon_usb_tester_DataPatternType_RANDOM;
    ASSERT_EQ(zircon_usb_tester_DeviceBulkLoopback(dev_svc, &params, &status), ZX_OK,
              "failed to call DeviceBulkLoopback");
    ASSERT_EQ(status, ZX_OK, "bulk loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K");

    close(dev_svc);
    END_TEST;
}

static bool usb_isoch_verify_result(zircon_usb_tester_TestParams* params,
                                    zircon_usb_tester_IsochResult* result) {
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

    zx_handle_t dev_svc;
    if (open_test_device(&dev_svc) != ZX_OK) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }
    ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "Invalid device service handle");

    zircon_usb_tester_TestParams params = {
        .data_pattern = zircon_usb_tester_DataPatternType_CONSTANT,
        .len =  64 * 1024
    };
    char err_msg1[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K";
    zx_status_t status;
    zircon_usb_tester_IsochResult result = {};
    ASSERT_EQ(zircon_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result), ZX_OK,
              "failed to call DeviceIsochLoopback");
    ASSERT_EQ(status, ZX_OK, err_msg1);
    ASSERT_TRUE(usb_isoch_verify_result(&params, &result), err_msg1);

    char err_msg2[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K";
    params.data_pattern = zircon_usb_tester_DataPatternType_RANDOM;
    ASSERT_EQ(zircon_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result), ZX_OK,
              "failed to call DeviceIsochLoopback");
    ASSERT_EQ(status, ZX_OK, err_msg2);
    ASSERT_TRUE(usb_isoch_verify_result(&params, &result), err_msg2);

    close(dev_svc);

    END_TEST;
}

BEGIN_TEST_CASE(usb_tests)
RUN_TEST(usb_root_hubs_test)
RUN_TEST(usb_bulk_loopback_test)
RUN_TEST(usb_isoch_loopback_test)
END_TEST_CASE(usb_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
