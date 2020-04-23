// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-spew.h"

#include <stdio.h>

#include <ddk/debug.h>

namespace mt_usb_hci {

void SpewUsbDeviceDescriptor(const usb_device_descriptor_t& d) {
  // Here and below, used until std::string is fully available.
  char buf[100];

  // clang-format off
    switch (d.bDeviceClass) {
    case USB_CLASS_AUDIO:                snprintf(buf, 100, "AUDIO"); break;
    case USB_CLASS_COMM:                 snprintf(buf, 100, "COMM"); break;
    case USB_CLASS_HID:                  snprintf(buf, 100, "HID"); break;
    case USB_CLASS_PHYSICAL:             snprintf(buf, 100, "PHYSICAL"); break;
    case USB_CLASS_IMAGING:              snprintf(buf, 100, "IMAGING"); break;
    case USB_CLASS_PRINTER:              snprintf(buf, 100, "PRINTER"); break;
    case USB_CLASS_MSC:                  snprintf(buf, 100, "MSC"); break;
    case USB_CLASS_HUB:                  snprintf(buf, 100, "HUB"); break;
    case USB_CLASS_CDC:                  snprintf(buf, 100, "CDC"); break;
    case USB_CLASS_CCID:                 snprintf(buf, 100, "CCID"); break;
    case USB_CLASS_SECURITY:             snprintf(buf, 100, "SECURITY"); break;
    case USB_CLASS_VIDEO:                snprintf(buf, 100, "VIDEO"); break;
    case USB_CLASS_HEALTHCARE:           snprintf(buf, 100, "HEALTHCARE"); break;
    case USB_CLASS_DIAGNOSTIC:           snprintf(buf, 100, "DIAGNOSTIC"); break;
    case USB_CLASS_WIRELESS:             snprintf(buf, 100, "WIRELESS"); break;
    case USB_CLASS_MISC:                 snprintf(buf, 100, "MISC"); break;
    case USB_CLASS_APPLICATION_SPECIFIC: snprintf(buf, 100, "APP-SPECIFIC"); break;
    case USB_CLASS_VENDOR:               snprintf(buf, 100, "VENDOR-SPECIFIC"); break;
    default:                             snprintf(buf, 100, "???"); break;
    }

    zxlogf(SPEW, "            ===  usb_device_descriptor_t ===");
    zxlogf(SPEW, "               .bLength = %d", d.bLength);
    zxlogf(SPEW, "               .bDescriptorType = <DEVICE_DESCRIPTOR>");
    zxlogf(SPEW, "               .bcdUSB = 0x%04x", d.bcdUSB);
    zxlogf(SPEW, "               .bDeviceClass = 0x%02x <%s>", d.bDeviceClass, buf);
    zxlogf(SPEW, "               .bDeviceSubClass = 0x%02x", d.bDeviceSubClass);
    zxlogf(SPEW, "               .bDeviceProtocol = 0x%02x", d.bDeviceProtocol);
    zxlogf(SPEW, "               .bMaxPacketSize0 = %d", d.bMaxPacketSize0);
    zxlogf(SPEW, "               .idVendor = 0x%04x", d.idVendor);
    zxlogf(SPEW, "               .idProduct = 0x%04x", d.idProduct);
    zxlogf(SPEW, "               .bcdDevice = 0x%04x", d.bcdDevice);
    zxlogf(SPEW, "               .iManufacturer = 0x%02x", d.iManufacturer);
    zxlogf(SPEW, "               .iProduct = 0x%02x", d.iProduct);
    zxlogf(SPEW, "               .iSerialNumber = 0x%02x", d.iSerialNumber);
    zxlogf(SPEW, "               .bNumConfigurations = %d", d.bNumConfigurations);
  // clang-format on
}

void SpewUsbEndpointDescriptor(const usb_endpoint_descriptor_t& d) {
  int off;
  char addr_buf[100];
  // Decode the endpoint address and direction.
  off = snprintf(addr_buf, 100, "ep=%d,", usb_ep_num(&d));
  off = snprintf(addr_buf + off, 100 - off, "%s", usb_ep_direction(&d) ? "IN" : "OUT");

  // Decode the endpoint type.
  char attr_buf[100];

  // clang-format off
    switch (static_cast<uint8_t>(usb_ep_type(&d))) {
    case USB_ENDPOINT_CONTROL:     snprintf(attr_buf, 100, "CONTROL"); break;
    case USB_ENDPOINT_ISOCHRONOUS: snprintf(attr_buf, 100, "ISOCHRONOUS"); break;
    case USB_ENDPOINT_BULK:        snprintf(attr_buf, 100, "BULK"); break;
    case USB_ENDPOINT_INTERRUPT:   snprintf(attr_buf, 100, "INTERRUPT"); break;
    default:                       snprintf(attr_buf, 100, "???"); break;
    }

    zxlogf(SPEW, "            ===  usb_endpoint_descriptor_t ===");
    zxlogf(SPEW, "               .bLength = %d", d.bLength);
    zxlogf(SPEW, "               .bDescriptorType = <ENDPOINT_DESCRIPTOR>");
    zxlogf(SPEW, "               .bEndpointAddress = 0x%02x <%s>", d.bEndpointAddress, addr_buf);
    zxlogf(SPEW, "               .bmAttributes = 0x%02x <%s>", d.bmAttributes, attr_buf);
    zxlogf(SPEW, "               .wMaxPacketSize = 0x%04x", d.wMaxPacketSize);
    zxlogf(SPEW, "               .bInterval = %d", d.bInterval);
  // clang-format on
}

void SpewUsbRequest(const usb_request_t& req) {
  char buf[100];

  // clang-format off
    switch (req.setup.bRequest) {
    case USB_REQ_GET_STATUS:        snprintf(buf, 100, "GET_STATUS"); break;
    case USB_REQ_CLEAR_FEATURE:     snprintf(buf, 100, "CLEAR_FEATURE"); break;
    case USB_REQ_SET_FEATURE:       snprintf(buf, 100, "SET_FEATURE"); break;
    case USB_REQ_SET_ADDRESS:       snprintf(buf, 100, "SET_ADDRESS"); break;
    case USB_REQ_GET_DESCRIPTOR:    snprintf(buf, 100, "GET_DESCRIPTOR"); break;
    case USB_REQ_SET_DESCRIPTOR:    snprintf(buf, 100, "SET_DESCRIPTOR"); break;
    case USB_REQ_GET_CONFIGURATION: snprintf(buf, 100, "GET_CONFIGURATION"); break;
    case USB_REQ_SET_CONFIGURATION: snprintf(buf, 100, "SET_CONFIGURATION"); break;
    case USB_REQ_GET_INTERFACE:     snprintf(buf, 100, "GET_INTERFACE"); break;
    case USB_REQ_SET_INTERFACE:     snprintf(buf, 100, "SET_INTERFACE"); break;
    case USB_REQ_SYNCH_FRAME:       snprintf(buf, 100, "SYNCH_FRAME"); break;
    default:                        snprintf(buf, 100, "???"); break;
    }

    zxlogf(SPEW, "            ===  usb_header_t ===");
    zxlogf(SPEW, "               .frame = %ld", req.header.frame);
    zxlogf(SPEW, "               .device_id = %d", req.header.device_id);
    zxlogf(SPEW, "               .ep_address = %d", req.header.ep_address);
    zxlogf(SPEW, "               .length = %ld", req.header.length);
    zxlogf(SPEW, "               .send_zlp = %d", req.header.send_zlp);
    zxlogf(SPEW, "            ===  usb_setup_t ===");
    zxlogf(SPEW, "               .bmRequestType = 0x%02x", req.setup.bmRequestType);
    zxlogf(SPEW, "               .bRequest = %d <%s>", req.setup.bRequest, buf);
    zxlogf(SPEW, "               .wValue = 0x%04x", req.setup.wValue);
    zxlogf(SPEW, "               .wIndex = 0x%04x", req.setup.wIndex);
    zxlogf(SPEW, "               .wLength = %d", req.setup.wLength);
  // clang-format on
}

}  // namespace mt_usb_hci
