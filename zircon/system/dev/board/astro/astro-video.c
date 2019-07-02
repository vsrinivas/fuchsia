// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zircon/syscalls/smc.h>

#include "astro.h"

static pbus_mmio_t astro_video_mmios[] = {
    {
        .base = S905D2_CBUS_BASE,
        .length = S905D2_CBUS_LENGTH,
    },
    {
        .base = S905D2_DOS_BASE,
        .length = S905D2_DOS_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    },
    {
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    },
};

static const pbus_bti_t astro_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static const pbus_irq_t astro_video_irqs[] = {
    {
        .irq = S905D2_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_smc_t astro_video_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        .exclusive = false,
    },
};

static const pbus_dev_t video_dev = {
    .name = "aml-video",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_VIDEO,
    .mmio_list = astro_video_mmios,
    .mmio_count = countof(astro_video_mmios),
    .bti_list = astro_video_btis,
    .bti_count = countof(astro_video_btis),
    .irq_list = astro_video_irqs,
    .irq_count = countof(astro_video_irqs),
    .smc_list = astro_video_smcs,
    .smc_count = countof(astro_video_smcs),
};

zx_status_t aml_video_init(aml_bus_t* bus) {
    zx_status_t status;
    if ((status = pbus_device_add(&bus->pbus, &video_dev)) != ZX_OK) {
        zxlogf(ERROR, "aml_video_init: pbus_device_add() failed for video: %d\n", status);
        return status;
    }
    return ZX_OK;
}
