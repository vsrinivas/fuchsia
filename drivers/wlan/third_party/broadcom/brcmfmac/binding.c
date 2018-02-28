// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

zx_status_t brcmfmac_bind(void* ctx, zx_device_t* device) {
  return ZX_ERR_NOT_SUPPORTED;
}
uint64_t jiffies; // To make it link, jiffies has to be defined (not just declared)
struct current_with_pid* current; // likewise current

static zx_driver_ops_t brcmfmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = brcmfmac_bind,
};

ZIRCON_DRIVER_BEGIN(brcmfmac, brcmfmac_driver_ops, "zircon", "0.1", 0)
//    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
//    BI_ABORT_IF(NE, BIND_USB_VID, 0x148f), // TODO cphoenix: use correct values
//    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5370),  // RT5370
//    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5572),  // RT5572
ZIRCON_DRIVER_END(brcmfmac)
