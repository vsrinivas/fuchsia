// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>

#include <zircon/hw/usb-video.h>

extern zx_status_t usb_video_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t usb_video_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_video_bind,
};

ZIRCON_DRIVER_BEGIN(usb_video, usb_video_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_VIDEO),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_video)
