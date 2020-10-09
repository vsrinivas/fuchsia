// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/usb.h>
#include <usb/usb.h>

#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <future>
#include <thread>

#include "device.h"
#include "logging.h"

#define QCA_GET_TARGET_VERSION 0x09
#define QCA_GET_STATUS 0x05
#define QCA_DFU_DOWNLOAD 0x01
#define QCA_SYSCFG_UPDATED 0x40
#define QCA_DFU_PACKET_LEN 4096
#define QCA_PATCH_UPDATED 0x80

zx_status_t bt_atheros_bind(void* ctx, zx_device_t* device) {
  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    errorf("couldn't get USB protocol: %s\n", zx_status_get_string(result));
    return result;
  }

  bt_hci_protocol_t hci;
  result = device_get_protocol(device, ZX_PROTOCOL_BT_HCI, &hci);
  if (result != ZX_OK) {
    errorf("couldn't get BT_HCI protocol: %s\n", zx_status_get_string(result));
    return result;
  }

  auto btdev = new btatheros::Device(device, &hci, &usb);
  result = btdev->Bind();
  if (result != ZX_OK) {
    errorf("failed binding device: %s\n", zx_status_get_string(result));
    delete btdev;
    return result;
  }

  // Bind succeeded and devmgr is now responsible for releasing |btdev|
  // The device's init hook will load the firmware.
  return ZX_OK;
}

static constexpr zx_driver_ops_t bt_atheros_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bt_atheros_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_hci_atheros, bt_atheros_driver_ops, "fuchsia", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_BT_TRANSPORT),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x0CF3), // Atheros Communications Inc.
    BI_MATCH_IF(EQ, BIND_USB_PID, 0xE300),
ZIRCON_DRIVER_END(bt_hci_atheros)
