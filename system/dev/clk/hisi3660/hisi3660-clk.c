// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>

#include <dev/clk/hisi-lib/hisi.h>
#include <soc/hi3660/hi3660-hw.h>


static hisi_clk_gate_t hi3660_clk_gates[] = {
    { .reg = 0x0, .bit = 0, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x0, .bit = 21, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x0, .bit = 30, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x0, .bit = 31, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 0, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 1, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 2, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 3, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 4, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 5, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 6, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 7, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 8, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 9, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 10, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 11, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 12, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 13, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 14, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 15, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 16, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 17, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 18, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 19, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 20, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 21, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 30, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x10, .bit = 31, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 7, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 9, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 11, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 12, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 14, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 15, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x20, .bit = 27, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 1, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 10, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 11, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 12, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 13, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 14, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 15, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 16, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 17, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 28, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 29, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 30, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x30, .bit = 31, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x40, .bit = 1, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x40, .bit = 4, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x40, .bit = 17, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x40, .bit = 19, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 16, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 17, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 18, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 21, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 28, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x50, .bit = 29, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x420, .bit = 5, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x420, .bit = 7, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x420, .bit = 8, .flags = HISI_CLK_FLAG_BANK_PERI },
    { .reg = 0x420, .bit = 9, .flags = HISI_CLK_FLAG_BANK_PERI },

    { .reg = 0x258, .bit = 7, .flags = HISI_CLK_FLAG_BANK_SCTRL },
    { .reg = 0x260, .bit = 11, .flags = HISI_CLK_FLAG_BANK_SCTRL },
    { .reg = 0x260, .bit = 12, .flags = HISI_CLK_FLAG_BANK_SCTRL },
    { .reg = 0x260, .bit = 13, .flags = HISI_CLK_FLAG_BANK_SCTRL },
    { .reg = 0x268, .bit = 11, .flags = HISI_CLK_FLAG_BANK_SCTRL },
};

static_assert(HI3660_SEP_CLK_GATE_COUNT == countof(hi3660_clk_gates),
              "hi3660_clk_gates[] and hisi_3660_sep_gate_clk_idx count mismatch");

static const char hi3660_clk_name[] = "hi3660-clk";

static zx_status_t hi3660_clk_bind(void* ctx, zx_device_t* parent) {
    return hisi_clk_init(hi3660_clk_name, hi3660_clk_gates,
                         countof(hi3660_clk_gates), parent);
}

static zx_driver_ops_t hi3660_clk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hi3660_clk_bind,
};

ZIRCON_DRIVER_BEGIN(hi3660_clk, hi3660_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HI3660_CLK),
ZIRCON_DRIVER_END(hi3660_clk)
