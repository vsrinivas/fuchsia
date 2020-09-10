// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-qmi-function.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <usb/usb-request.h>

namespace usb_qmi_function {

constexpr uint8_t kQmiInitRequest[]{1, 15, 0, 0, 0, 0, 0, 1, 34, 0, 4, 0, 1, 1, 0, 2};
constexpr uint8_t kQmiInitResp[]{1, 23, 0, 128, 0, 0, 1, 1, 34, 0, 12, 0,
                                 2, 4,  0, 0,   0, 0, 0, 1, 2,  0, 2,  1};
constexpr uint8_t kQmiImeiReq[]{1, 12, 0, 0, 2, 1, 0, 1, 0, 37, 0, 0, 0};
constexpr uint8_t kQmiImeiResp[]{1,  41, 0,  128, 2,  1,  2,  1,  0,  37, 0,  29, 0,  2,
                                 4,  0,  0,  0,   0,  0,  16, 1,  0,  48, 17, 15, 0,  51,
                                 53, 57, 50, 54,  48, 48, 56, 48, 49, 54, 56, 51, 53, 49};
constexpr uint8_t kQmiNonsenseResp[]{1, 0};
constexpr uint8_t kQmiHwMemUninitVal = 170;

size_t FakeUsbQmiFunction::UsbFunctionInterfaceGetDescriptorsSize() { return descriptor_size_; }

static void ReplyQmiMsg(const void* req, uint32_t req_size, void* resp, size_t resp_buf_size,
                        size_t* resp_size) {
  memset(resp, kQmiHwMemUninitVal, resp_buf_size);  // uninitialized buffer have value of 170 in hw
  const uint8_t* msg = static_cast<const uint8_t*>(req);
  if (!memcmp(req, kQmiInitRequest, sizeof(kQmiInitRequest))) {
    *resp_size = std::min(sizeof(kQmiInitResp), resp_buf_size);
    memcpy(resp, kQmiInitResp, *resp_size);
  } else if (!memcmp(req, kQmiImeiReq, sizeof(kQmiImeiReq))) {
    *resp_size = std::min(sizeof(kQmiImeiResp), resp_buf_size);
    memcpy(resp, kQmiImeiResp, *resp_size);
  } else {
    zxlogf(INFO, "FakeUsbQmiFunction: unknown cmd received: %u,%u,%u,%u,%u,%u,%u,%u", msg[0],
           msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7]);
    *resp_size = std::min(sizeof(kQmiNonsenseResp), resp_buf_size);
    memcpy(resp, kQmiNonsenseResp, *resp_size);
  }
}

void FakeUsbQmiFunction::UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer,
                                                            size_t descriptors_size,
                                                            size_t* out_descriptors_actual) {
  memcpy(out_descriptors_buffer, &descriptor_, std::min(descriptors_size, descriptor_size_));
  *out_descriptors_actual = std::min(descriptors_size, descriptor_size_);
}

void FakeUsbQmiFunction::QmiCompletionCallback(usb_request_t* req) {
  assert(usb_int_req_.has_value() == false);
  usb_int_req_ = usb_req_temp_;
  zxlogf(INFO, "FakeUsbQmiFunction: interrupt sent successfully");
}

zx_status_t FakeUsbQmiFunction::UsbFunctionInterfaceControl(const usb_setup_t* setup,
                                                            const void* write_buffer,
                                                            size_t write_size,
                                                            void* out_read_buffer, size_t read_size,
                                                            size_t* out_read_actual) {
  zxlogf(INFO, "FakeUsbQmiFunction: received write buffer in endpoint, req_type:x%X",
         setup->bmRequestType);
  if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
    if (setup->bRequest == 0) {
      ReplyQmiMsg(write_buffer, write_size, tx_data_, 256, &tx_size_);
      if (usb_int_req_) {
        usb_request_complete_t complete = {
            .callback =
                [](void* ctx, usb_request_t* req) {
                  return static_cast<FakeUsbQmiFunction*>(ctx)->QmiCompletionCallback(req);
                },
            .ctx = this,
        };
        usb_cdc_notification_t cdc_notification;
        cdc_notification.bNotification = USB_CDC_NC_RESPONSE_AVAILABLE;
        usb_request_t* req = usb_int_req_.value();
        usb_request_copy_to(req, &cdc_notification, sizeof(cdc_notification), 0);
        usb_int_req_.reset();
        function_.RequestQueue(req, &complete);
      } else {
        zxlogf(ERROR, "FakeUsbQmiFunction: interrupt req not queued");
      }
      return ZX_OK;
    }
  }
  if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
    memcpy(out_read_buffer, tx_data_, tx_size_);
    *out_read_actual = tx_size_;
    zxlogf(INFO, "FakeUsbQmiFunction: successfully txed data");
    return ZX_OK;
  }
  if (out_read_actual) {
    *out_read_actual = 0;
  }
  return ZX_OK;
}

zx_status_t FakeUsbQmiFunction::UsbFunctionInterfaceSetConfigured(bool configured,
                                                                  usb_speed_t speed) {
  (void)configured;
  (void)speed;
  return ZX_OK;
}
zx_status_t FakeUsbQmiFunction::UsbFunctionInterfaceSetInterface(uint8_t interface,
                                                                 uint8_t alt_setting) {
  (void)interface;
  (void)alt_setting;
  return ZX_OK;
}

zx_status_t FakeUsbQmiFunction::Bind() {
  descriptor_size_ = sizeof(FakeUsbQmiDescriptor);

  descriptor_.interface = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 8,
      .bAlternateSetting = 0,
      .bNumEndpoints = 3,
      .bInterfaceClass = USB_CLASS_VENDOR,
      .bInterfaceSubClass = USB_SUBCLASS_VENDOR,
      .bInterfaceProtocol = 0xFF,
      .iInterface = 0,
  };
  descriptor_.interrupt = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,
      .bmAttributes = USB_ENDPOINT_INTERRUPT,
      .wMaxPacketSize = htole16(0x8),
      .bInterval = 9,
  };
  descriptor_.bulk_in = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(0x200),
      .bInterval = 0,
  };
  descriptor_.bulk_out = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_OUT,
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(0x200),
      .bInterval = 0,
  };

  for (uint32_t i = 0; i < kUsbIntfDummySize; i++) {
    descriptor_.interface_dummy[i].bDescriptorType = USB_DT_INTERFACE;
    descriptor_.interface_dummy[i].bLength = sizeof(usb_interface_descriptor_t);
    zx_status_t status = function_.AllocInterface(&descriptor_.interface_dummy[i].bInterfaceNumber);
    if (status != ZX_OK) {
      zxlogf(ERROR, "FakeUsbQmiFunction: usb_function_alloc_interface failed");
      return status;
    }
  }
  zx_status_t status = function_.AllocInterface(&descriptor_.interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbQmiFunction: usb_function_alloc_interface failed");
    return status;
  }
  assert(descriptor_.interface.bInterfaceNumber == 8);
  status = function_.AllocEp(USB_DIR_IN, &descriptor_.interrupt.bEndpointAddress);
  assert(descriptor_.interrupt.bEndpointAddress != 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbQmiFunction: usb_function_alloc_ep failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_IN, &descriptor_.bulk_in.bEndpointAddress);
  assert(descriptor_.bulk_in.bEndpointAddress != 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbQmiFunction: usb_function_alloc_ep failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_OUT, &descriptor_.bulk_out.bEndpointAddress);
  assert(descriptor_.bulk_out.bEndpointAddress != 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbQmiFunction: usb_function_alloc_ep failed");
    return status;
  }
  usb_request_t* usb_int_req_ptr;
  status = usb_request_alloc(&usb_int_req_ptr, 0x200, descriptor_.interrupt.bEndpointAddress,
                             function_.GetRequestSize());
  if (status != ZX_OK) {
    return status;
  }
  usb_int_req_ = usb_int_req_ptr;
  usb_req_temp_ = usb_int_req_ptr;
  status = DdkAdd("usb-qmi-function");
  if (status != ZX_OK) {
    return status;
  }
  function_.SetInterface(this, &usb_function_interface_protocol_ops_);
  return ZX_OK;
}

void FakeUsbQmiFunction::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void FakeUsbQmiFunction::DdkRelease() {
  if (usb_int_req_) {
    usb_request_release(usb_int_req_.value());
  } else if (usb_req_temp_) {
    usb_request_release(usb_req_temp_.value());
  }
  delete this;
}

zx_status_t bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<FakeUsbQmiFunction>(parent);
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
  ops.bind = bind;
  return ops;
}();

}  // namespace usb_qmi_function

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_qmi_function, usb_qmi_function::driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_VENDOR),
ZIRCON_DRIVER_END(usb_qmi_function)
    // clang-format on
