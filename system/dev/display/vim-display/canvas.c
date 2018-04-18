// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>
#include <hw/reg.h>
#include "vim-display.h"
#include "hdmitx.h"

/* Add a framebuffer to the canvas lookup table */
bool add_canvas_entry(vim2_display_t* display, zx_paddr_t paddr, uint8_t* idx)
{
    unsigned i;
    for (i = 0; i < NUM_CANVAS_ENTRIES; i++) {
        if ((display->canvas_entries[i / 8] & (1 << (i % 8))) == 0) {
            display->canvas_entries[i / 8] |= (1 << (i % 8));
            break;
        }
    }
    if (i == NUM_CANVAS_ENTRIES) {
        return false;
    }
    *idx = i;

    uint32_t fbh = display->height;
    uint32_t fbw = display->stride * ZX_PIXEL_FORMAT_BYTES(display->format);

    DISP_INFO("Canvas Diminsions: w=%d h=%d\n", fbw, fbh);

    // set framebuffer address in DMC, read/modify/write
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL,
        (((paddr + 7) >> 3) & DMC_CAV_ADDR_LMASK) |
             ((((fbw + 7) >> 3) & DMC_CAV_WIDTH_LMASK) << DMC_CAV_WIDTH_LBIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH,
        ((((fbw + 7) >> 3) >> DMC_CAV_WIDTH_LWID) << DMC_CAV_WIDTH_HBIT) |
             ((fbh & DMC_CAV_HEIGHT_MASK) << DMC_CAV_HEIGHT_BIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | *idx);
    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

    return true;
}

void free_canvas_entry(vim2_display_t* display, uint8_t idx) {
    display->canvas_entries[idx / 8] &= ~(1 << (idx % 8));
}
