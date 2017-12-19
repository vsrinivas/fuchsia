// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "bootloader-display.h"

namespace i915 {

BootloaderDisplay::BootloaderDisplay(Controller* controller, uint16_t device_id,
                                     registers::Ddi ddi, registers::Pipe pipe)
    : DisplayDevice(controller, device_id, ddi, pipe) {}

bool BootloaderDisplay::Init(zx_display_info_t* di) {
    uint32_t format, width, height, stride;
    zx_status_t status = zx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == ZX_OK) {
        di->format = format;
        di->width = width;
        di->height = height;
        di->stride = stride;
    } else {
        di->format = ZX_PIXEL_FORMAT_RGB_565;
        di->width = 2560 / 2;
        di->height = 1700 / 2;
        di->stride = 2560 / 2;
    }
    di->flags = ZX_DISPLAY_FLAG_HW_FRAMEBUFFER;
    if ((di->pixelsize = ZX_PIXEL_FORMAT_BYTES(di->format)) == 0) {
        zxlogf(ERROR, "i915: unknown format %u\n", di->format);
        return false;
    }

    return true;
}

} // namespace i915
