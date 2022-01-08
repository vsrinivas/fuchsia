// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/usb-peripheral-test.h>
#include <zircon/device/usb-peripheral.h>

#include <thread>

#include <usbhost/usbhost.h>
#include <zxtest/zxtest.h>

namespace {

struct usb_device* dev = nullptr;
struct usb_endpoint_descriptor* bulk_out_ep = nullptr;
struct usb_endpoint_descriptor* bulk_in_ep = nullptr;
struct usb_endpoint_descriptor* intr_ep = nullptr;

constexpr size_t BUFFER_SIZE = 4096;

// Data to send to the device
uint8_t send_buf[BUFFER_SIZE] = {};
// Buffer for receiving data from the device
uint8_t receive_buf[BUFFER_SIZE] = {};

constexpr int TIMEOUT = 1000;  // 1 seecond

// Interface number for the test interface
static uint8_t test_interface;

// Fill send_buf with random values.
void randomize() {
  // Generate some random data.
  for (size_t i = 0; i < sizeof(send_buf); i++) {
    send_buf[i] = static_cast<uint8_t>(random());
  }
}

// Tests control and interrupt transfers with specified transfer size.
void control_interrupt_test(size_t transfer_size) {
  randomize();

  // Send data to device via OUT control request.
  int ret = usb_device_control_transfer(
      dev, USB_DIR_OUT | USB_TYPE_VENDOR | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
      USB_PERIPHERAL_TEST_SET_DATA, 0, test_interface, send_buf, static_cast<int>(transfer_size),
      TIMEOUT);
  EXPECT_EQ(ret, static_cast<int>(transfer_size));

  // Receive data back from device via IN control request.
  ret = usb_device_control_transfer(
      dev, USB_DIR_IN | USB_TYPE_VENDOR | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
      USB_PERIPHERAL_TEST_GET_DATA, 0, test_interface, receive_buf, static_cast<int>(transfer_size),
      TIMEOUT);
  EXPECT_EQ(ret, static_cast<int>(transfer_size));

  // Sent and received data should match.
  EXPECT_EQ(memcmp(send_buf, receive_buf, transfer_size), 0);

  // Create a thread to wait for interrupt request.
  auto thread_func = [](struct usb_request** req) -> void {
    *req = usb_request_wait(dev, TIMEOUT);
  };
  struct usb_request* complete_req = nullptr;
  std::thread wait_thread(thread_func, &complete_req);

  // Queue read for interrupt request
  auto* req = usb_request_new(dev, intr_ep);
  EXPECT_NE(req, nullptr);
  req->buffer = receive_buf;
  req->buffer_length = static_cast<int>(transfer_size);
  ret = usb_request_queue(req);
  EXPECT_EQ(ret, 0);

  // Ask the device to send us an interrupt request containing the data we sent earlier.
  ret = usb_device_control_transfer(
      dev, USB_DIR_OUT | USB_TYPE_VENDOR | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
      USB_PERIPHERAL_TEST_SEND_INTERUPT, 0, test_interface, nullptr, 0, TIMEOUT);
  EXPECT_EQ(ret, 0);

  wait_thread.join();

  EXPECT_EQ(complete_req, req);
  EXPECT_EQ(static_cast<size_t>(req->actual_length), transfer_size);

  // Sent data should match payload of interrupt request.
  EXPECT_EQ(memcmp(send_buf, receive_buf, transfer_size), 0);

  usb_request_free(req);
}

// Test control and interrupt requests with 8 byte transfer size.
TEST(UsbPeripheral, control_interrupt_test_8) {
  ASSERT_NO_FATAL_FAILURE(control_interrupt_test(8));
}

// Test control and interrupt requests with 64 byte transfer size.
TEST(UsbPeripheral, control_interrupt_test_64) {
  ASSERT_NO_FATAL_FAILURE(control_interrupt_test(64));
}

// Test control and interrupt requests with 100 byte transfer size.
TEST(UsbPeripheral, control_interrupt_test_100) {
  ASSERT_NO_FATAL_FAILURE(control_interrupt_test(100));
}

// Test control and interrupt requests with 256 byte transfer size.
TEST(UsbPeripheral, control_interrupt_test_256) {
  ASSERT_NO_FATAL_FAILURE(control_interrupt_test(256));
}

// Test control and interrupt requests with 1000 byte transfer size.
TEST(UsbPeripheral, control_interrupt_test_1000) {
  ASSERT_NO_FATAL_FAILURE(control_interrupt_test(1000));
}

// Tests bulk OUT and IN transfers.
// Send BUFFER_SIZE bytes to device, read back and compare.
TEST(UsbPeripheral, bulk_test) {
  auto* send_req = usb_request_new(dev, bulk_out_ep);
  EXPECT_NE(send_req, nullptr);
  send_req->buffer = send_buf;
  send_req->buffer_length = static_cast<int>(BUFFER_SIZE);

  auto* receive_req = usb_request_new(dev, bulk_in_ep);
  EXPECT_NE(send_req, nullptr);
  receive_req->buffer = receive_buf;
  receive_req->buffer_length = static_cast<int>(BUFFER_SIZE);

  for (int i = 0; i < 10; i++) {
    randomize();

    // Create a thread to wait for request completions.
    auto thread_func = [](struct usb_request** reqs) -> void {
      *reqs++ = usb_request_wait(dev, TIMEOUT);
      *reqs = usb_request_wait(dev, TIMEOUT);
    };
    struct usb_request* complete_reqs[2] = {};
    std::thread wait_thread(thread_func, complete_reqs);

    // Queue requests in both directions
    int ret = usb_request_queue(receive_req);
    EXPECT_EQ(ret, 0);
    ret = usb_request_queue(send_req);
    EXPECT_EQ(ret, 0);

    wait_thread.join();

    EXPECT_NE(complete_reqs[0], nullptr);
    EXPECT_NE(complete_reqs[1], nullptr);

    // Sent and received data should match.
    EXPECT_EQ(memcmp(send_buf, receive_buf, BUFFER_SIZE), 0);
  }

  usb_request_free(send_req);
  usb_request_free(receive_req);
}

// usb_host_load() will call this for all connected USB devices.
int usb_device_added(const char* dev_name, void* client_data) {
  usb_descriptor_iter iter;
  struct usb_descriptor_header* header;
  struct usb_interface_descriptor* intf = nullptr;
  int ret;

  dev = usb_device_open(dev_name);
  if (!dev) {
    fprintf(stderr, "usb_device_open failed for %s\n", dev_name);
    return 0;
  }

  uint16_t vid = usb_device_get_vendor_id(dev);
  uint16_t pid = usb_device_get_product_id(dev);

  if (vid != GOOGLE_USB_VID ||
      (pid != GOOGLE_USB_FUNCTION_TEST_PID && pid != GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID)) {
    // Device doesn't match, so keep looking.
    usb_device_close(dev);
    dev = nullptr;
    return 0;
  }

  usb_descriptor_iter_init(dev, &iter);

  while ((header = usb_descriptor_iter_next(&iter)) != nullptr) {
    if (header->bDescriptorType == USB_DT_INTERFACE) {
      intf = reinterpret_cast<struct usb_interface_descriptor*>(header);
    } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
      auto* ep = reinterpret_cast<struct usb_endpoint_descriptor*>(header);
      if (usb_endpoint_type(ep) == USB_ENDPOINT_XFER_BULK) {
        if (usb_endpoint_dir_in(ep)) {
          bulk_in_ep = ep;
        } else {
          bulk_out_ep = ep;
        }
      } else if (usb_endpoint_type(ep) == USB_ENDPOINT_XFER_INT) {
        intr_ep = ep;
      }
    }
  }

  if (!intf || !bulk_out_ep || !bulk_in_ep || !intr_ep) {
    fprintf(stderr, "could not find all our endpoints\n");
    goto fail;
  }

  ret = usb_device_claim_interface(dev, intf->bInterfaceNumber);
  if (ret < 0) {
    fprintf(stderr, "usb_device_claim_interface failed\n");
    goto fail;
  }
  test_interface = intf->bInterfaceNumber;

  // Device found, exit from usb_host_load().
  return 1;

fail:
  usb_device_close(dev);
  dev = nullptr;
  // Test done, exit from usb_host_load().
  return 1;
}

int usb_device_removed(const char* dev_name, void* client_data) { return 0; }

int usb_discovery_done(void* client_data) { return 0; }

}  // anonymous namespace

int main(int argc, char** argv) {
  struct usb_host_context* context = usb_host_init();
  if (!context) {
    fprintf(stderr, "usb_host_context failed\n");
    return -1;
  }
  auto cleanup = fit::defer([&]() { usb_host_cleanup(context); });

  auto ret =
      usb_host_load(context, usb_device_added, usb_device_removed, usb_discovery_done, nullptr);
  if (ret < 0) {
    fprintf(stderr, "usb_host_load failed!\n");
    goto fail;
  }
  if (!dev) {
    fprintf(stderr, "No device found, skipping tests.\n");
    goto fail;
  }

  ret = zxtest::RunAllTests(argc, argv) ? 0 : -1;

fail:
  if (dev) {
    usb_device_close(dev);
  }

  return ret;
}
