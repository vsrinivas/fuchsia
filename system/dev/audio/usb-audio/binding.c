// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/audio.h>

extern zx_status_t usb_audio_device_bind(void*, zx_device_t*);
extern void usb_audio_driver_release(void*);

static zx_driver_ops_t usb_audio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_audio_device_bind,
    .release = usb_audio_driver_release,
};

ZIRCON_DRIVER_BEGIN(usb_audio, usb_audio_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_AUDIO),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_AUDIO_CONTROL),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_audio)
