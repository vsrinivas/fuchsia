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
#include <ddk/protocol/clk.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <dev/clk/meson-lib/meson.h>

typedef struct meson_clk {
    platform_device_protocol_t pdev;
    clk_protocol_t clk;
    zx_device_t* zxdev;
    io_buffer_t mmio;

    meson_clk_gate_t* gates;
    size_t gate_count;

    // Serialize access to clocks.
    mtx_t lock;
} meson_clk_t;

static zx_status_t meson_clk_toggle(void* ctx, const uint32_t idx,
                                    const bool enable) {
    meson_clk_t* const meson_clk = ctx;

    if (idx >= meson_clk->gate_count) return ZX_ERR_INVALID_ARGS;

    const meson_clk_gate_t* const gate = &meson_clk->gates[idx];

    volatile uint32_t* regs = (volatile uint32_t*)io_buffer_virt(&meson_clk->mmio);

    mtx_lock(&meson_clk->lock);

    uint32_t val = readl(regs + gate->reg);

    if (enable) {
        val |= (1 << gate->bit);
    } else {
        val &= ~(1 << gate->bit);
    }

    writel(val, regs + gate->reg);

    mtx_unlock(&meson_clk->lock);

    return ZX_OK;
}

static zx_status_t meson_clk_enable(void* ctx, uint32_t clk) {
    return meson_clk_toggle(ctx, clk, true);
}

static zx_status_t meson_clk_disable(void* ctx, uint32_t clk) {
    return meson_clk_toggle(ctx, clk, false);
}

clk_protocol_ops_t clk_ops = {
    .enable = meson_clk_enable,
    .disable = meson_clk_disable,
};

static void meson_clk_release(void* ctx) {
    meson_clk_t* clk = ctx;
    io_buffer_release(&clk->mmio);
    mtx_destroy(&clk->lock);
    free(clk);
}


static zx_protocol_device_t meson_clk_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = meson_clk_release,
};

zx_status_t meson_clk_init(const char* name, meson_clk_gate_t* gates,
                           const size_t gate_count, zx_device_t* parent) {
    zx_status_t st = ZX_ERR_INTERNAL;

    meson_clk_t* meson_clk = calloc(1, sizeof(*meson_clk));
    if (!meson_clk) {
    zxlogf(ERROR, "meson_clk_bind: failed to allocate meson_clk, "
                      "st = %d\n", ZX_ERR_NO_MEMORY);
        return ZX_ERR_NO_MEMORY;
    }

    meson_clk->gates = gates;
    meson_clk->gate_count = gate_count;

    int ret = mtx_init(&meson_clk->lock, mtx_plain);
    if (ret != thrd_success) {
        st = thrd_status_to_zx_status(ret);
        zxlogf(ERROR, "meson_clk_bind: failed to initialize mutex, st = %d\n",
               st);
        goto fail;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &meson_clk->pdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: failed to get ZX_PROTOCOL_PLATFORM_DEV, "
                      "st = %d\n", st);
        goto fail;
    }

    platform_bus_protocol_t pbus;
    st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: failed to get ZX_PROTOCOL_PLATFORM_BUS, "
               "st = %d\n", st);
        goto fail;
    }

    st = pdev_map_mmio_buffer(&meson_clk->pdev, 0,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &meson_clk->mmio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: failed to map clk mmio, st = %d\n", st);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = meson_clk,
        .ops = &meson_clk_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    st = device_add(parent, &args, &meson_clk->zxdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: device_add failed, st = %d\n", st);
        goto fail;
    }

    meson_clk->clk.ops = &clk_ops;
    meson_clk->clk.ctx = meson_clk;

    st = pbus_set_protocol(&pbus, ZX_PROTOCOL_CLK, &meson_clk->clk);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: pbus_set_protocol failed, st = %d\n", st);
        goto fail;
    }

    return ZX_OK;

fail:
    meson_clk_release(meson_clk);

    // Make sure we don't accidentally return ZX_OK if the device has failed
    // to bind for some reason
    ZX_DEBUG_ASSERT(st != ZX_OK);
    return st;
}