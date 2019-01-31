// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/sdmmc.h>
#include <hw/sdio.h>

#include "sdmmc.h"
#include "sdio.h"

zx_status_t sdio_enable_interrupt(void *ctx, uint8_t fn_idx) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = ctx;

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    if (func->intr_enabled) {
        return ZX_OK;
    }

    uint8_t intr_byte;
    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IEN_INTR_EN_ADDR, 0,
                           &intr_byte);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_interrupt: Failed to enable interrupt for fn: %d status: %d\n",
               fn_idx, st);
        return st;
    }

    // Enable fn intr
    intr_byte |= 1 << fn_idx;
    // Enable master intr
    intr_byte |= 1;

    st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_IEN_INTR_EN_ADDR,
                           intr_byte, NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_interrupt: Failed to enable interrupt for fn: %d status: %d\n",
               fn_idx, st);
        return st;
    }

    func->intr_enabled = true;
    zxlogf(TRACE, "sdio_enable_interrupt: Interrupt enabled for fn %d\n", fn_idx);
    return ZX_OK;
}

zx_status_t sdio_disable_interrupt(void *ctx, uint8_t fn_idx) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = ctx;

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }
    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    if (!(func->intr_enabled)) {
        zxlogf(ERROR, "sdio_disable_interrupt: Interrupt is not enabled for %d\n", fn_idx);
        return ZX_ERR_BAD_STATE;
    }

    uint8_t intr_byte;
    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IEN_INTR_EN_ADDR, 0,
                           &intr_byte);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_disable_interrupt: Failed reading intr enable reg."
               " func: %d status: %d\n", fn_idx, st);
        return st;
    }

    intr_byte &= ~(1 << fn_idx);
    if (!(intr_byte & SDIO_ALL_INTR_ENABLED_MASK)) {
        //disable master as well
        intr_byte = 0;
    }

    st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_IEN_INTR_EN_ADDR, intr_byte, NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_disable_interrupt: Error writing to intr enable reg."
               " func: %d status: %d\n", fn_idx, st);
        return st;
    }

    func->intr_enabled = false;
    zxlogf(TRACE, "sdio_enable_interrupt: Interrupt disabled for fn %d\n", fn_idx);
    return ZX_OK;
}
