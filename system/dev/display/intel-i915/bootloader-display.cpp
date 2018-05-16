// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <string.h>

#include "bootloader-display.h"

namespace i915 {

BootloaderDisplay::BootloaderDisplay(Controller* controller, uint64_t id,
                                     registers::Ddi ddi, registers::Pipe pipe)
        : DisplayDevice(controller, id, ddi, registers::TRANS_A, pipe) {}

bool BootloaderDisplay::QueryDevice(edid::Edid* edid, zx_display_info_t* di) {
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

    // Create a fake EDID that only advertises one the bootloader fb size. Really hacky,
    // but that's all we can do if we don't know enough to read a real EDID.
    uint8_t header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    memcpy(&fake_base_edid_.header, header, fbl::count_of(header));

    fake_base_edid_.edid_version = 1;
    fake_base_edid_.edid_revision = 3;

    fake_base_edid_.preferred_timing.horizontal_addressable_low = di->width & 0xff;
    fake_base_edid_.preferred_timing.set_horizontal_addressable_high(
            static_cast<uint8_t>(di->width >> 8));
    fake_base_edid_.preferred_timing.vertical_addressable_low = di->height & 0xff;
    fake_base_edid_.preferred_timing.set_vertical_addressable_high(
            static_cast<uint8_t>(di->height >> 8));
    fake_base_edid_.preferred_timing.pixel_clock_10khz =
            static_cast<uint16_t>(di->width * di->height * 30 / 10000);

    fake_base_edid_.num_extensions = 0;
    uint8_t sum = 0;
    for (unsigned i = 0; i < edid::kBlockSize; i++) {
        sum = static_cast<uint8_t>(sum + reinterpret_cast<uint8_t*>(&fake_base_edid_)[i]);
    }
    fake_base_edid_.checksum_byte = static_cast<uint8_t>(0 - sum);

    const char* err_msg;
    return edid->Init(reinterpret_cast<uint8_t*>(&fake_base_edid_),
                      sizeof(edid::BaseEdid), &err_msg);
}

bool BootloaderDisplay::DefaultModeset() {
    // We don't support doing anything, so just hope something already set
    // the hardware up.
    return true;
}

} // namespace i915
