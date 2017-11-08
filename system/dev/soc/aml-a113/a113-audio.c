// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <zircon/assert.h>

#include "a113-bus.h"

#define PDM_MMIO_BASE 0xff632000
#define EE_AUDIO_MMIO_BASE 0xff642000
#define PDM_IRQ (85 + 32)

static const pbus_mmio_t audio_mmios[] = {
    {
        .base = EE_AUDIO_MMIO_BASE, .length = PAGE_SIZE,
    },
    {
        .base = PDM_MMIO_BASE, .length = PAGE_SIZE,
    },
};

static const pbus_irq_t audio_irqs[] = {
    {
        .irq = PDM_IRQ,
    },
};

static const pbus_dev_t gauss_audio_dev = {
    .name = "gauss-audio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_A113,
    .did = PDEV_DID_AMLOGIC_GAUSS_AUDIO,
    .mmios = audio_mmios,
    .mmio_count = countof(audio_mmios),
    .irqs = audio_irqs,
    .irq_count = countof(audio_irqs),
};

zx_status_t a113_audio_init(a113_bus_t* bus) {
    ZX_DEBUG_ASSERT(bus);
    zx_status_t status;

    // Various hardware initialization and configuration will go here

    // Add audio device.
    if ((status = pbus_device_add(&bus->pbus, &gauss_audio_dev, 0)) !=
        ZX_OK) {
        zxlogf(ERROR, "a113_audio_init could not add gauss_audio_dev: %d\n",
               status);
    }

    return status;
}
