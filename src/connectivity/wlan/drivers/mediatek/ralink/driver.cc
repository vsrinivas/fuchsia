// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <future>
#include <thread>
#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <usb/usb.h>
#include <wlan/common/logging.h>

#include "device.h"

zx_status_t ralink_bind(void* ctx, zx_device_t* device) {
  zxlogf(DEBUG, "%s", __func__);

  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    return result;
  }

  size_t parent_req_size = usb_get_request_size(&usb);
  ZX_DEBUG_ASSERT(parent_req_size != 0);

  usb_desc_iter_t iter;
  result = usb_desc_iter_init(&usb, &iter);
  if (result < 0)
    return result;

  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bNumEndpoints < 3) {
    usb_desc_iter_release(&iter);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t blkin_endpt = 0;
  std::vector<uint8_t> blkout_endpts;

  auto endpt = usb_desc_iter_next_endpoint(&iter);
  while (endpt) {
    if (usb_ep_direction(endpt) == USB_ENDPOINT_OUT) {
      blkout_endpts.push_back(endpt->bEndpointAddress);
    } else if (usb_ep_type(endpt) == USB_ENDPOINT_BULK) {
      blkin_endpt = endpt->bEndpointAddress;
    }
    endpt = usb_desc_iter_next_endpoint(&iter);
  }
  usb_desc_iter_release(&iter);

  if (!blkin_endpt || blkout_endpts.empty()) {
    zxlogf(ERROR, "%s could not find endpoints", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto rtdev =
      new ralink::Device(device, usb, blkin_endpt, std::move(blkout_endpts), parent_req_size);
  zx_status_t status = rtdev->Bind();
  if (status != ZX_OK) {
    delete rtdev;
  }

  return status;
}

static constexpr zx_driver_ops_t ralink_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ralink_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(ralink, ralink_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x148f),
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5370),  // RT5370
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5572),  // RT5572
ZIRCON_DRIVER_END(ralink)
    // clang-format on
