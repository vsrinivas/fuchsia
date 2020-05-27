// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/tester/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <unistd.h>

#include <unittest/unittest.h>

#define USB_TESTER_DEV_DIR "/dev/class/usb-tester"
#define USB_DEVICE_DEV_DIR "/dev/class/usb-device"

#define ISOCH_MIN_PASS_PERCENT 80
#define ISOCH_MIN_PACKETS 10lu

static DIR* open_usb_device_dir(void) { return opendir(USB_DEVICE_DEV_DIR); }

static zx_status_t check_xhci_root_hubs(DIR* d) {
  struct dirent* de;
  uint8_t count = 0;
  while ((de = readdir(d)) != NULL) {
    count++;
  }
  // TODO(ravoorir): Use FIDL apis to read the descriptors
  // of the devices and ensure that both 2.0 root hub and
  // 3.0 root hub showed up.
  if (count < 2) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

static bool usb_root_hubs_test(void) {
  BEGIN_TEST;
  // TODO(ravoorir): Wait for /dev/class/usb
  // to be created.
  DIR* d = open_usb_device_dir();
  if (d == NULL) {
    // TODO(ravoorir): At the moment we cannot restrict a test
    // to only run on hardware(IN-497) and not the qemu instances.
    // We should fail here when running on hardware.
    unittest_printf_critical(" Root hub creation failed.[SKIPPING]");
    return true;
  }
  // TODO(ravoorir): There should be a matrix of hardware that should
  // be accessible from here. Depending on whether the hardware has
  // xhci/ehci, we should check the root hubs.
  zx_status_t status = check_xhci_root_hubs(d);
  if (status != ZX_OK) {
    unittest_printf_critical(" Root hub creation failed.[SKIPPING]");
    closedir(d);
    return true;
  }
  closedir(d);
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

  fuchsia_hardware_usb_tester_BulkTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT, .len = 64 * 1024};
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceBulkLoopback(dev_svc, &params, NULL, NULL, &status),
            ZX_OK, "failed to call DeviceBulkLoopback");
  ASSERT_EQ(status, ZX_OK, "bulk loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K");

  params.data_pattern = fuchsia_hardware_usb_tester_DataPatternType_RANDOM;
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceBulkLoopback(dev_svc, &params, NULL, NULL, &status),
            ZX_OK, "failed to call DeviceBulkLoopback");
  ASSERT_EQ(status, ZX_OK, "bulk loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K");

  close(dev_svc);
  END_TEST;
}

static bool usb_bulk_scatter_gather_test(void) {
  BEGIN_TEST;

  zx_handle_t dev_svc;
  if (open_test_device(&dev_svc) != ZX_OK) {
    unittest_printf_critical(" [SKIPPING]");
    return true;
  }
  ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "invalid device service handle");

  fuchsia_hardware_usb_tester_BulkTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_RANDOM,
      .len = 230,
  };
  fuchsia_hardware_usb_tester_SgList sg_list = {
      .entries =
          {
              {.length = 10, .offset = 100},
              {.length = 30, .offset = 1000},
              {.length = 100, .offset = 4000},
              {.length = 40, .offset = 5000},
              {.length = 50, .offset = 10000},
          },
      .len = 5,
  };

  zx_status_t status;
  ASSERT_EQ(
      fuchsia_hardware_usb_tester_DeviceBulkLoopback(dev_svc, &params, &sg_list, NULL, &status),
      ZX_OK, "failed to call DeviceBulkLoopback");
  ASSERT_EQ(status, ZX_OK,
            "bulk loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K with scatter gather OUT");

  ASSERT_EQ(
      fuchsia_hardware_usb_tester_DeviceBulkLoopback(dev_svc, &params, NULL, &sg_list, &status),
      ZX_OK, "failed to call DeviceBulkLoopback");
  ASSERT_EQ(status, ZX_OK,
            "bulk loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K with scatter gather IN");

  close(dev_svc);
  END_TEST;
}

static bool usb_isoch_verify_result(fuchsia_hardware_usb_tester_IsochResult* result) {
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

  fuchsia_hardware_usb_tester_IsochTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT,
      .num_packets = 64,
      .packet_size = 1024};
  char err_msg1[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_CONSTANT 64 K";
  zx_status_t status;
  fuchsia_hardware_usb_tester_IsochResult result = {};
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, err_msg1);
  ASSERT_TRUE(usb_isoch_verify_result(&result), err_msg1);

  char err_msg2[] = "isoch loopback failed: USB_TESTER_DATA_PATTERN_RANDOM 64 K";
  params.data_pattern = fuchsia_hardware_usb_tester_DataPatternType_RANDOM;
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, err_msg2);
  ASSERT_TRUE(usb_isoch_verify_result(&result), err_msg2);

  close(dev_svc);

  END_TEST;
}

static bool usb_callbacks_opt_out_test(void) {
  BEGIN_TEST;

  zx_handle_t dev_svc;
  if (open_test_device(&dev_svc) != ZX_OK) {
    unittest_printf_critical(" [SKIPPING]");
    return true;
  }
  ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "Invalid device service handle");

  fuchsia_hardware_usb_tester_IsochTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT,
      .num_packets = 64,
      .packet_size = 1024,
      .packet_opts_len = params.num_packets};
  size_t reqs_per_callback = 10;
  for (size_t i = 0; i < params.num_packets; ++i) {
    // Set a callback on every 10 requests, and also on the last request.
    bool set_cb = ((i + 1) % reqs_per_callback == 0) || (i == params.num_packets - 1);
    params.packet_opts[i].set_cb = set_cb;
    params.packet_opts[i].expect_cb = set_cb;
  }

  zx_status_t status;
  fuchsia_hardware_usb_tester_IsochResult result = {};
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_TRUE(usb_isoch_verify_result(&result), "callbacks test failed: 10 reqs per callback");

  close(dev_svc);

  END_TEST;
}

static bool usb_single_callback_error_test(void) {
  BEGIN_TEST;

  zx_handle_t dev_svc;
  if (open_test_device(&dev_svc) != ZX_OK) {
    unittest_printf_critical(" [SKIPPING]");
    return true;
  }
  ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "Invalid device service handle");

  // We should always get a callback on error.
  fuchsia_hardware_usb_tester_IsochTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT,
      .num_packets = 1,
      .packet_size = 1024,
      .packet_opts = {{.set_cb = false, .set_error = true, .expect_cb = true}},
      .packet_opts_len = 1};
  char err_msg[] = "callbacks on error test failed: set_cb = false, set_error = true";
  zx_status_t status;
  fuchsia_hardware_usb_tester_IsochResult result = {};
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, err_msg);
  // Don't need to verify the transfer results since we only care about callbacks for this test.

  close(dev_svc);

  END_TEST;
}

static bool usb_callbacks_on_error_test(void) {
  BEGIN_TEST;

  zx_handle_t dev_svc;
  if (open_test_device(&dev_svc) != ZX_OK) {
    unittest_printf_critical(" [SKIPPING]");
    return true;
  }
  ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "Invalid device service handle");

  // Error on the last packet receiving a callback.
  fuchsia_hardware_usb_tester_IsochTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT,
      .num_packets = 4,
      .packet_size = 1024,
      .packet_opts = {{.set_cb = false, .set_error = false, .expect_cb = false},
                      {.set_cb = false, .set_error = true, .expect_cb = true},
                      {.set_cb = false, .set_error = false, .expect_cb = true},
                      {.set_cb = true, .set_error = true, .expect_cb = true}},
      .packet_opts_len = 4};
  char err_msg[] = "callbacks on error test failed: error on last packet receiving callback";
  zx_status_t status;
  fuchsia_hardware_usb_tester_IsochResult result = {};
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, err_msg);
  // Don't need to verify the transfer results since we only care about callbacks for this test.

  close(dev_svc);

  END_TEST;
}

static bool usb_callbacks_on_multiple_errors_test(void) {
  BEGIN_TEST;

  zx_handle_t dev_svc;
  if (open_test_device(&dev_svc) != ZX_OK) {
    unittest_printf_critical(" [SKIPPING]");
    return true;
  }
  ASSERT_NE(dev_svc, ZX_HANDLE_INVALID, "Invalid device service handle");

  fuchsia_hardware_usb_tester_IsochTestParams params = {
      .data_pattern = fuchsia_hardware_usb_tester_DataPatternType_CONSTANT,
      .num_packets = 10,
      .packet_size = 1024,
      .packet_opts = {{.set_cb = false, .set_error = false, .expect_cb = false},
                      {.set_cb = false, .set_error = false, .expect_cb = true},
                      {.set_cb = false, .set_error = true, .expect_cb = true},
                      {.set_cb = true, .set_error = true, .expect_cb = true},
                      {.set_cb = false, .set_error = false, .expect_cb = false},
                      {.set_cb = false, .set_error = true, .expect_cb = true},
                      {.set_cb = false, .set_error = false, .expect_cb = false},
                      {.set_cb = true, .set_error = false, .expect_cb = true},
                      {.set_cb = false, .set_error = true, .expect_cb = true},
                      {.set_cb = true, .set_error = false, .expect_cb = true}},
      .packet_opts_len = 10};
  char err_msg[] = "callbacks on error test failed: multiple errors";
  zx_status_t status;
  fuchsia_hardware_usb_tester_IsochResult result = {};
  ASSERT_EQ(fuchsia_hardware_usb_tester_DeviceIsochLoopback(dev_svc, &params, &status, &result),
            ZX_OK, "failed to call DeviceIsochLoopback");
  ASSERT_EQ(status, ZX_OK, err_msg);
  // Don't need to verify the transfer results since we only care about callbacks for this test.

  close(dev_svc);

  END_TEST;
}

BEGIN_TEST_CASE(usb_tests)
RUN_TEST(usb_root_hubs_test)
RUN_TEST(usb_bulk_loopback_test)
RUN_TEST(usb_bulk_scatter_gather_test)
RUN_TEST(usb_isoch_loopback_test)
RUN_TEST(usb_callbacks_opt_out_test)
RUN_TEST(usb_single_callback_error_test)
RUN_TEST(usb_callbacks_on_error_test)
RUN_TEST(usb_callbacks_on_multiple_errors_test)
END_TEST_CASE(usb_tests)
