// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <gpio/pl061/pl061.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

#include <soc/hi3660/hi3660.h>
#include <soc/hi3660/hi3660-hw.h>

zx_status_t hi3660_init(zx_handle_t resource, hi3660_t** out) {
    hi3660_t* hi3660 = calloc(1, sizeof(hi3660_t));
    if (!hi3660) {
        return ZX_ERR_NO_MEMORY;
    }
    list_initialize(&hi3660->gpios);

    zx_status_t status;
    if ((status = io_buffer_init_physical(&hi3660->usb3otg_bc, MMIO_USB3OTG_BC_BASE,
                                          MMIO_USB3OTG_BC_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = io_buffer_init_physical(&hi3660->peri_crg, MMIO_PERI_CRG_BASE,
                                           MMIO_PERI_CRG_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = io_buffer_init_physical(&hi3660->pctrl, MMIO_PCTRL_BASE, MMIO_PCTRL_LENGTH,
                                           resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK) {
        goto fail;
    }

    status = hi3660_gpio_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }
    status = hi3660_usb_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }

    *out = hi3660;
    return ZX_OK;

fail:
    zxlogf(ERROR, "hi3660_init failed %d\n", status);
    hi3660_release(hi3660);
    return status;
}

zx_status_t hi3660_get_protocol(hi3660_t* hi3660, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_GPIO: {
        memcpy(out, &hi3660->gpio, sizeof(hi3660->gpio));
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void hi3660_release(hi3660_t* hi3660) {
    hi3660_gpio_release(hi3660);
    io_buffer_release(&hi3660->usb3otg_bc);
    io_buffer_release(&hi3660->peri_crg);
    io_buffer_release(&hi3660->pctrl);
    free(hi3660);
}
