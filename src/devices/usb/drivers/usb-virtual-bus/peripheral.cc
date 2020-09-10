// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peripheral.h"

#include <assert.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <vector>

namespace virtualbus {

void TestFunction::CompletionCallback(usb_request_t* req) {
  usb::Request<> request(req, parent_req_size_);
}

size_t TestFunction::UsbFunctionInterfaceGetDescriptorsSize() { return descriptor_size_; }

void TestFunction::UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer,
                                                      size_t descriptors_size,
                                                      size_t* out_descriptors_actual) {
  memcpy(out_descriptors_buffer, &descriptor_, std::min(descriptors_size, descriptor_size_));
  *out_descriptors_actual = descriptor_size_;
}

zx_status_t TestFunction::UsbFunctionInterfaceControl(const usb_setup_t* setup,
                                                      const void* write_buffer, size_t write_size,
                                                      void* out_read_buffer, size_t read_size,
                                                      size_t* out_read_actual) {
  if (out_read_actual) {
    *out_read_actual = 0;
  }
  return ZX_OK;
}

zx_status_t TestFunction::UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed) {
  if (configured) {
    if (configured_) {
      return ZX_OK;
    }
    configured_ = true;
    function_.ConfigEp(&descriptor_.bulk_out, nullptr);

    // queue first read on OUT endpoint
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* req) {
              return static_cast<TestFunction*>(ctx)->CompletionCallback(req);
            },
        .ctx = this,
    };
    std::optional<usb::Request<>> data_out_req;
    usb::Request<>::Alloc(&data_out_req, kMaxPacketSize, bulk_out_addr_, parent_req_size_);
    function_.RequestQueue(data_out_req->take(), &complete);
  } else {
    configured_ = false;
  }
  return ZX_OK;
}

zx_status_t TestFunction::UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting) {
  return ZX_OK;
}

zx_status_t TestFunction::Bind() {
  descriptor_size_ = sizeof(descriptor_);
  descriptor_.interface = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 1,
      .bInterfaceClass = 0xFF,
      .bInterfaceSubClass = 0xFF,
      .bInterfaceProtocol = 0xFF,
      .iInterface = 0,
  };
  descriptor_.bulk_out = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_OUT,
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = 512,
      .bInterval = 0,
  };

  active_ = true;

  parent_req_size_ = function_.GetRequestSize();

  zx_status_t status = function_.AllocInterface(&descriptor_.interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_function_alloc_interface failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_OUT, &descriptor_.bulk_out.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_function_alloc_ep failed");
    return status;
  }

  bulk_out_addr_ = descriptor_.bulk_out.bEndpointAddress;

  status = DdkAdd("virtual-bus-test-peripheral");
  if (status != ZX_OK) {
    return status;
  }
  function_.SetInterface(this, &usb_function_interface_protocol_ops_);

  return ZX_OK;
}

void TestFunction::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestFunction::DdkRelease() { delete this; }

zx_status_t Bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<TestFunction>(parent);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }
  return ZX_OK;
}
static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Bind;
  return ops;
}();

}  // namespace virtualbus

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_virtual_bus_tester, virtualbus::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_VID, virtualbus::kVid),
    BI_ABORT_IF(NE, BIND_USB_PID, virtualbus::kDid),
    BI_MATCH()
ZIRCON_DRIVER_END(usb_virtual_bus_tester)
    // clang-format on
