// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <zircon/assert.h>

#include "gauss.h"

#define PDM_MMIO_BASE 0xff632000
#define EE_AUDIO_MMIO_BASE 0xff642000
#define PDM_IRQ (85 + 32)

static const pbus_mmio_t audio_in_mmios[] = {
    {
        .base = EE_AUDIO_MMIO_BASE, .length = PAGE_SIZE,
    },
    {
        .base = PDM_MMIO_BASE, .length = PAGE_SIZE,
    },
};

static const pbus_irq_t audio_in_irqs[] = {
    {
        .irq = PDM_IRQ,
    },
};

static const pbus_dev_t gauss_audio_in_dev = {
    .name = "gauss-audio-in",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_GAUSS,
    .did = PDEV_DID_GAUSS_AUDIO_IN,
    .mmios = audio_in_mmios,
    .mmio_count = countof(audio_in_mmios),
    .irqs = audio_in_irqs,
    .irq_count = countof(audio_in_irqs),
};

static const pbus_dev_t gauss_audio_out_dev = {
    .name = "gauss-audio-in",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_GAUSS,
    .did = PDEV_DID_GAUSS_AUDIO_OUT,
};

zx_status_t gauss_audio_init(gauss_bus_t* bus) {
    ZX_DEBUG_ASSERT(bus);
    zx_status_t status;

    // Add audio in and out devices.
    if ((status = pbus_device_add(&bus->pbus, &gauss_audio_in_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_audio_init could not add gauss_audio_in_dev: %d\n", status);
        return status;
    }
    if ((status = pbus_device_add(&bus->pbus, &gauss_audio_out_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_audio_init could not add gauss_audio_out_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
