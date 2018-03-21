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
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>
#include <hw/reg.h>
#include "vim-display.h"
#include "hdmitx.h"

/* Configure Canvas memory to point to the FrameBuffer allocated by the display driver */
zx_status_t configure_canvas(vim2_display_t* display)
{
    uint32_t fbh = display->disp_info.height * 2;
    uint32_t fbw = display->disp_info.width * 4;

    DISP_INFO("Canvas Diminsions: w=%d h=%d\n", fbw, fbh);

    // set framebuffer address in DMC, read/modify/write
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL,
        (((io_buffer_phys(&display->fbuffer) + 7) >> 3) & DMC_CAV_ADDR_LMASK) |
             ((((fbw + 7) >> 3) & DMC_CAV_WIDTH_LMASK) << DMC_CAV_WIDTH_LBIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH,
        ((((fbw + 7) >> 3) >> DMC_CAV_WIDTH_LWID) << DMC_CAV_WIDTH_HBIT) |
             ((fbh & DMC_CAV_HEIGHT_MASK) << DMC_CAV_HEIGHT_BIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | OSD2_DMC_CAV_INDEX );
    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

    return ZX_OK;
}
