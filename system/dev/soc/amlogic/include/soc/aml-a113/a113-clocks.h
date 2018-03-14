// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>

#define SDM_FRACTIONALITY       ((uint32_t)16384)
#define A113_FIXED_PLL_RATE     ((uint32_t)2000000000)
#define A113_CLOCKS_BASE_PHYS   0xff63c000

// Clock register offsets (all are 32-bit registers)
#define A113_HHI_MPLL_CNTL         (0xa0)
#define A113_HHI_MPLL_CNTL8        (0xa8)
#define A113_HHI_PLL_TOP_MISC      (0xba)

typedef struct {
    io_buffer_t     regs_iobuff;
    zx_vaddr_t      virt_regs;
} a113_clk_dev_t;

static inline uint32_t a113_clk_get_reg(a113_clk_dev_t *dev, uint32_t offset) {
    return   ((volatile uint32_t *)dev->virt_regs)[offset];
}

static inline uint32_t a113_clk_set_reg(a113_clk_dev_t *dev, uint32_t offset, uint32_t value) {
    ((uint32_t *)dev->virt_regs)[offset] = value;
    return   ((volatile uint32_t *)dev->virt_regs)[offset];
}

zx_status_t a113_clk_init(zx_handle_t bti, a113_clk_dev_t **device);
zx_status_t a113_clk_set_mpll2(a113_clk_dev_t *device, uint64_t rate,
                               uint64_t *actual);
