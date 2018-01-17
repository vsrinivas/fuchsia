// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
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

extern "C" zx_status_t btintel_bind(void* ctx, zx_device_t* device) {
  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
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
    return result;
  }

  auto btdev = new btintel::Device(device, &hci);
  auto f = std::async(std::launch::async, [btdev, secure]() {
    zx_status_t status;
    status = btdev->Bind(secure);
    if (status != ZX_OK) {
      errorf("failed to bind: %s\n", zx_status_get_string(status));
      delete btdev;
    }
    // Bind succeeded and devmgr is responsible for releasing |btdev|
  });
  return ZX_OK;
}
