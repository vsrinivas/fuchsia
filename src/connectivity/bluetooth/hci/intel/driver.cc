// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/usb.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <future>
#include <thread>

#include "device.h"
#include "logging.h"

// USB Product IDs that use the "secure" firmware method.
constexpr uint16_t sfi_product_ids[] = {0x0025, 0x0a2b, 0x0aaa};

zx_status_t btintel_bind(void* ctx, zx_device_t* device) {
  tracef("bind\n");

  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    errorf("couldn't get USB protocol: %s\n", zx_status_get_string(result));
    return result;
  }

  usb_device_descriptor_t dev_desc;
  usb_get_device_descriptor(&usb, &dev_desc);

  // Whether this device uses the "secure" firmware method.
  bool secure = false;
  for (uint16_t id : sfi_product_ids) {
    if (dev_desc.idProduct == id) {
      secure = true;
      break;
    }
  }

  bt_hci_protocol_t hci;
  result = device_get_protocol(device, ZX_PROTOCOL_BT_HCI, &hci);
  if (result != ZX_OK) {
    errorf("couldn't get BT_HCI protocol: %s\n", zx_status_get_string(result));
    return result;
  }

  auto btdev = new btintel::Device(device, &hci);
  result = btdev->Bind();
  if (result != ZX_OK) {
    errorf("failed binding device: %s\n", zx_status_get_string(result));
    delete btdev;
    return result;
  }
  // Bind succeeded and devmgr is now responsible for releasing |btdev|
  auto f = std::async(std::launch::async, [btdev, secure]() { btdev->LoadFirmware(secure); });
  return ZX_OK;
}

static constexpr zx_driver_ops_t btintel_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = btintel_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_hci_intel, btintel_driver_ops, "fuchsia", "0.1", 8)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_BT_TRANSPORT),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x8087), // Intel Corp.
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x07dc), // Intel 7260
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0a2a), // Intel 7265
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0aa7), // Sandy Peak (3168)
    // Devices below use the "secure" method
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0025), // Thunder Peak (9160/9260)
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0a2b), // Snowfield Peak (8260)
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0aaa), // Jefferson Peak (9460/9560)
ZIRCON_DRIVER_END(bt_hci_intel)
