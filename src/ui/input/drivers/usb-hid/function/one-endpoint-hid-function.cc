// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "one-endpoint-hid-function.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <usb/usb-request.h>

constexpr int BULK_MAX_PACKET = 512;

namespace one_endpoint_hid_function {

static const uint8_t boot_mouse_r_desc[50] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (0x01)
    0x29, 0x03,  //     Usage Maximum (0x03)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position)
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

size_t FakeUsbHidFunction::UsbFunctionInterfaceGetDescriptorsSize(void* ctx) {
  FakeUsbHidFunction* func = static_cast<FakeUsbHidFunction*>(ctx);
  return func->descriptor_size_;
}

void FakeUsbHidFunction::UsbFunctionInterfaceGetDescriptors(void* ctx, void* out_descriptors_buffer,
                                                            size_t descriptors_size,
                                                            size_t* out_descriptors_actual) {
  FakeUsbHidFunction* func = static_cast<FakeUsbHidFunction*>(ctx);
  memcpy(out_descriptors_buffer, func->descriptor_.get(),
         std::min(descriptors_size, func->descriptor_size_));
  *out_descriptors_actual = func->descriptor_size_;
}

zx_status_t FakeUsbHidFunction::UsbFunctionInterfaceControl(void* ctx, const usb_setup_t* setup,
                                                            const void* write_buffer,
                                                            size_t write_size,
                                                            void* out_read_buffer, size_t read_size,
                                                            size_t* out_read_actual) {
  FakeUsbHidFunction* func = static_cast<FakeUsbHidFunction*>(ctx);

  if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE)) {
    if (setup->bRequest == USB_REQ_GET_DESCRIPTOR) {
      memcpy(out_read_buffer, func->report_desc_.data(), func->report_desc_.size());
      *out_read_actual = func->report_desc_.size();
      return ZX_OK;
    }
  }
  if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
    if (setup->bRequest == USB_HID_GET_REPORT) {
      memcpy(out_read_buffer, func->report_.data(), func->report_.size());
      *out_read_actual = func->report_.size();
      return ZX_OK;
    }
    if (setup->bRequest == USB_HID_GET_PROTOCOL) {
      memcpy(out_read_buffer, &func->hid_protocol_, sizeof(func->hid_protocol_));
      *out_read_actual = sizeof(func->hid_protocol_);
      return ZX_OK;
    }
  }
  if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
    if (setup->bRequest == USB_HID_SET_REPORT) {
      memcpy(func->report_.data(), write_buffer, func->report_.size());
      return ZX_OK;
    }
    if (setup->bRequest == USB_HID_SET_PROTOCOL) {
      func->hid_protocol_ = static_cast<uint8_t>(setup->wValue);
      return ZX_OK;
    }
  }
  return ZX_ERR_IO_REFUSED;
}

zx_status_t FakeUsbHidFunction::UsbFunctionInterfaceSetConfigured(void* ctx, bool configured,
                                                                  usb_speed_t speed) {
  return ZX_OK;
}
zx_status_t FakeUsbHidFunction::UsbFunctionInterfaceSetInterface(void* ctx, uint8_t interface,
                                                                 uint8_t alt_setting) {
  return ZX_OK;
}

zx_status_t FakeUsbHidFunction::Bind() {
  report_desc_.resize(sizeof(boot_mouse_r_desc));
  memcpy(report_desc_.data(), &boot_mouse_r_desc, sizeof(boot_mouse_r_desc));
  report_.resize(3);

  descriptor_size_ = sizeof(fake_usb_hid_descriptor_t) + sizeof(usb_hid_descriptor_entry_t);
  descriptor_.reset(static_cast<fake_usb_hid_descriptor_t*>(calloc(1, descriptor_size_)));
  descriptor_->interface = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 1,
      .bInterfaceClass = USB_CLASS_HID,
      .bInterfaceSubClass = USB_HID_SUBCLASS_BOOT,
      .bInterfaceProtocol = USB_HID_PROTOCOL_MOUSE,
      .iInterface = 0,
  };
  descriptor_->interrupt = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,  // set later
      .bmAttributes = USB_ENDPOINT_INTERRUPT,
      .wMaxPacketSize = htole16(BULK_MAX_PACKET),
      .bInterval = 8,
  };
  descriptor_->hid_descriptor = {
      .bLength = sizeof(usb_hid_descriptor_t) + sizeof(usb_hid_descriptor_entry_t),
      .bDescriptorType = USB_DT_HID,
      .bcdHID = 0,
      .bCountryCode = 0,
      .bNumDescriptors = 1,
  };
  descriptor_->hid_descriptor.descriptors[0] = {
      .bDescriptorType = 0x22,  // HID TYPE REPORT
      .wDescriptorLength = static_cast<uint16_t>(report_desc_.size()),
  };

  zx_status_t status = function_.AllocInterface(&descriptor_->interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbHidFunction: usb_function_alloc_interface failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_IN, &descriptor_->interrupt.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbHidFunction: usb_function_alloc_ep failed");
    return status;
  }

  status = DdkAdd("usb-hid-function");
  if (status != ZX_OK) {
    return status;
  }
  function_.SetInterface(this, &function_interface_ops_);
  return ZX_OK;
}

void FakeUsbHidFunction::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void FakeUsbHidFunction::DdkRelease() { delete this; }

zx_status_t bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<FakeUsbHidFunction>(parent);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }
  return ZX_OK;
}
static constexpr zx_driver_ops_t one_endpoint_hid_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();

}  // namespace one_endpoint_hid_function

// clang-format off
ZIRCON_DRIVER_BEGIN(one_endpoint_hid_function, one_endpoint_hid_function::one_endpoint_hid_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_HID),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_TEST_HID_ONE_ENDPOINT),
ZIRCON_DRIVER_END(one_endpoint_hid_function)
    // clang-format on
