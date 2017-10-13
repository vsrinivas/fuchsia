// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <zircon/process.h>
#include <zircon/syscalls/resource.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"

void platform_init_resources(platform_resources_t* resources, uint32_t mmio_count,
                             uint32_t irq_count) {
    resources->mmio_count = mmio_count;
    resources->irq_count = irq_count;

    if (mmio_count > 0) {
        resources->mmios = (platform_mmio_t *)resources->extra;
    } else {
        resources->mmios = NULL;
    }
    if (irq_count > 0) {
        resources->irqs = (platform_irq_t *)&resources->extra[mmio_count * sizeof(platform_mmio_t)];
    } else {
        resources->irqs = NULL;
    }
}

zx_status_t platform_bus_add_mmios(platform_bus_t* bus, platform_resources_t* resources,
                                   const pbus_mmio_t* pbus_mmios, size_t mmio_count) {
    platform_mmio_t* mmios = resources->mmios;

    for (size_t i = 0; i < mmio_count; i++) {
        const pbus_mmio_t* pbus_mmio = pbus_mmios++;
        zx_paddr_t base = pbus_mmio->base;
        size_t length = pbus_mmio->length;

        if (!base || !length) {
            dprintf(ERROR, "platform_add_mmios: missing base or length\n");
            return ZX_ERR_INVALID_ARGS;
        }

        mmios->base = base;
        mmios->length = length;
        mmios++;
    }

    return ZX_OK;
}

zx_status_t platform_bus_add_irqs(platform_bus_t* bus, platform_resources_t* resources,
                                  const pbus_irq_t* pbus_irqs, size_t irq_count) {
    platform_irq_t* irqs = resources->irqs;

    for (size_t i = 0; i < irq_count; i++) {
        const pbus_irq_t* pbus_irq = pbus_irqs++;
        uint32_t irq = pbus_irq->irq;
        irqs->irq = irq;
        irqs++;
    }

    return ZX_OK;
}
