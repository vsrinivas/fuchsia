// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <future>
#include <thread>

#include "device.h"

extern "C" zx_status_t btintel_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);

  bt_hci_protocol_t hci;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_BT_HCI, &hci);
  if (result != ZX_OK) {
    return result;
  }

  auto btdev = new btintel::Device(device, &hci);
  auto f = std::async(std::launch::async, [btdev]() {
    auto status = btdev->Bind();
    if (status != ZX_OK) {
      std::printf("btintel: failed to bind: %s\n",
                  zx_status_get_string(status));
      delete btdev;
    }
    // Bind succeeded and devmgr is responsible for releasing |btdev|
  });
  return ZX_OK;
}
