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
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-device-lib.h>

#include <dev/clk/hisi-lib/hisi.h>

// HiSilicon has two different types of clock gates:
//
// + Clock Gates
//   These are enabled and disabled by setting and unsetting bits in the
//   sctrl_mmio register bank. Setting a bit to 1 enables the corresponding
//   clock and 0 disables it.
//
// + Separated Clock Gates
//   These are enabled via one bank of registers and disabled via another.
//   Writing 1 to a clock's enable bit will enable it and writing 1 to its
//   disable bank will disable it.


// These constants only apply to separated clock gates and correspond to the
// offset from the register base that needs to be modified to enable/disable
// the clock.
#define SEP_ENABLE  (0x0)
#define SEP_DISABLE (0x1)
#define SEP_STATUS  (0x2)

typedef struct hisi_clk {
    pdev_protocol_t pdev;
    clk_protocol_t clk;
    zx_device_t* zxdev;

    mmio_buffer_t peri_crg_mmio;  // Separated Clock Gates
    mmio_buffer_t sctrl_mmio;     // Regular Clock Gates

    hisi_clk_gate_t* gates;
    size_t gate_count;

    // Serialize access to clocks.
    mtx_t lock;
} hisi_clk_t;

static void hisi_sep_clk_toggle_locked(volatile uint8_t* reg,
                                       const uint32_t bit, const bool enable) {
    const uint32_t val = 1 << bit;

    volatile uint32_t* base = (volatile uint32_t*)reg;

    if (enable) {
        writel(val, base + SEP_ENABLE);
    } else {
        writel(val, base + SEP_DISABLE);
    }

    readl(base + SEP_STATUS);
}

static void hisi_gate_clk_toggle_locked(volatile uint8_t* reg,
                                        const uint32_t bit, const bool enable) {
    uint32_t val = readl(reg);

    if (enable) {
        val |= 1 << bit;
    } else {
        val &= ~(1 << bit);
    }

    writel(val, reg);
}

static zx_status_t hisi_clk_toggle(void* ctx, const uint32_t idx,
                                    const bool enable) {
    hisi_clk_t* const hisi_clk = ctx;

    if (idx >= hisi_clk->gate_count) return ZX_ERR_INVALID_ARGS;

    const hisi_clk_gate_t* const gate = &hisi_clk->gates[idx];

    const uint32_t flags = gate->flags;

    // Select the register bank depending on which bank this clock belongs to.
    mtx_lock(&hisi_clk->lock);
    if (HISI_CLK_FLAG_BANK(flags) == HISI_CLK_FLAG_BANK_SCTRL) {
        volatile uint8_t* base =
            (volatile uint8_t*)hisi_clk->sctrl_mmio.vaddr;
        hisi_gate_clk_toggle_locked(base + gate->reg, gate->bit, enable);
    } else if (HISI_CLK_FLAG_BANK(flags) == HISI_CLK_FLAG_BANK_PERI) {
        volatile uint8_t* base =
            (volatile uint8_t*)hisi_clk->peri_crg_mmio.vaddr;
        hisi_sep_clk_toggle_locked(base + gate->reg, gate->bit, enable);
    } else {
        // Maybe you passed an unimplemented clock bank?
        ZX_DEBUG_ASSERT(false);
    }
    mtx_unlock(&hisi_clk->lock);

    return ZX_OK;
}

static zx_status_t hisi_clk_enable(void* ctx, uint32_t clk) {
    return hisi_clk_toggle(ctx, clk, true);
}

static zx_status_t hisi_clk_disable(void* ctx, uint32_t clk) {
    return hisi_clk_toggle(ctx, clk, false);
}

clk_protocol_ops_t clk_ops = {
    .enable = hisi_clk_enable,
    .disable = hisi_clk_disable,
};

static void hisi_clk_release(void* ctx) {
    hisi_clk_t* clk = ctx;
    mmio_buffer_release(&clk->peri_crg_mmio);
    mmio_buffer_release(&clk->sctrl_mmio);
    mtx_destroy(&clk->lock);
    free(clk);
}


static zx_protocol_device_t hisi_clk_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = hisi_clk_release,
};

static void hisi_validate_gates(const hisi_clk_gate_t* gates, const size_t n) {
    // A clock can't exist in both banks.
    const uint32_t kBadFlagMask =
        (HISI_CLK_FLAG_BANK_SCTRL | HISI_CLK_FLAG_BANK_PERI);

    for (size_t i = 0; i < n; i++) {
        ZX_DEBUG_ASSERT(HISI_CLK_FLAG_BANK(gates[i].flags) != kBadFlagMask);
    }
}

zx_status_t hisi_clk_init(const char* name, hisi_clk_gate_t* gates,
                          const size_t gate_count, zx_device_t* parent) {
    zx_status_t st = ZX_ERR_INTERNAL;

    hisi_validate_gates(gates, gate_count);

    hisi_clk_t* hisi_clk = calloc(1, sizeof(*hisi_clk));
    if (!hisi_clk) {
    zxlogf(ERROR, "hisi_clk_bind: failed to allocate hisi_clk, "
                      "st = %d\n", ZX_ERR_NO_MEMORY);
        return ZX_ERR_NO_MEMORY;
    }

    hisi_clk->gates = gates;
    hisi_clk->gate_count = gate_count;

    int ret = mtx_init(&hisi_clk->lock, mtx_plain);
    if (ret != thrd_success) {
        st = thrd_status_to_zx_status(ret);
        zxlogf(ERROR, "hisi_clk_bind: failed to initialize mutex, st = %d\n",
               st);
        goto fail;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &hisi_clk->pdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: failed to get ZX_PROTOCOL_PDEV, "
                      "st = %d\n", st);
        goto fail;
    }

    pbus_protocol_t pbus;
    st = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: failed to get ZX_PROTOCOL_PBUS, "
               "st = %d\n", st);
        goto fail;
    }

    // Map in MMIO for separated clock gates.
    st = pdev_map_mmio_buffer2(&hisi_clk->pdev, 0,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &hisi_clk->peri_crg_mmio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: failed to map MMIO_PERI_CRG, st = %d\n", st);
        goto fail;
    }


    // Map in MMIO for regular clock gates.
    st = pdev_map_mmio_buffer2(&hisi_clk->pdev, 1,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &hisi_clk->sctrl_mmio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: failed to map MMIO_SCTRL, st = %d\n", st);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = hisi_clk,
        .ops = &hisi_clk_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    st = device_add(parent, &args, &hisi_clk->zxdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: device_add failed, st = %d\n", st);
        goto fail;
    }

    hisi_clk->clk.ops = &clk_ops;
    hisi_clk->clk.ctx = hisi_clk;

    const platform_proxy_cb_t kCallback = {NULL, NULL};
    st = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLK, &hisi_clk->clk, sizeof(hisi_clk->clk),
                                &kCallback);
    if (st != ZX_OK) {
        zxlogf(ERROR, "hisi_clk_bind: pbus_register_protocol failed, st = %d\n", st);
        goto fail;
    }

    return ZX_OK;

fail:
    hisi_clk_release(hisi_clk);

    // Make sure we don't accidentally return ZX_OK if the device has failed
    // to bind for some reason
    ZX_DEBUG_ASSERT(st != ZX_OK);
    return st;
}
