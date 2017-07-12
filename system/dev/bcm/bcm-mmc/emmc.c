// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes and limitations:
// 1. This driver _almost_ implements the standard SDHCI spec but doesn't quite
//    conform entirely due to idiosyncrasies in the Pi3's controller. For
//    example, this driver relies on the VC-mailbox device to get the base clock
//    rate for the device and to power the device on. Additionally, the Pi3's
//    controller does not appear to support any type of DMA natively and relies
//    on the BCM28xx's DMA controller for DMA. For this reason, this driver uses
//    PIO to communicate with the device. A more complete (and generic) driver
//    might attempt [S/A]DMA and fall back on PIO in case of failure.
//    Additionally, the Pi's controller doesn't appear to populate the SDHCI
//    capabilities registers to expose what capabilities the EMMC controller
//    provides.
//
// 2. This driver only supports SDHCv3 and above. Lower versions of SD are not
//    currently supported. The driver should fail gracefully if a lower version
//    card is detected.

// Standard Includes
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/bcm-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/sdhci.h>

#include <magenta/process.h>

// BCM28xx Specific Includes
#include <bcm/bcm28xx.h>
#include <bcm/ioctl.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define PAGE_MASK_4K (0xFFF)
#define SDMMC_PAGE_START (EMMC_BASE & (~PAGE_MASK_4K))
#define SDMMC_PAGE_SIZE (0x1000)

#define MMIO_INDEX  0
#define IRQ_INDEX   0


typedef struct emmc {
    mx_device_t* mxdev;
    mx_device_t* parent;
   platform_device_protocol_t pdev;
   void* mmio_base;
   size_t mmio_size;
   mx_handle_t mmio_handle;
} emmc_t;

static mx_handle_t emmc_sdhci_get_interrupt(void* ctx) {
    emmc_t* emmc = ctx;

    mx_handle_t handle;
    if (pdev_map_interrupt(&emmc->pdev, IRQ_INDEX, &handle) == MX_OK) {
        return handle;
    } else {
        return MX_HANDLE_INVALID;
    }
}

static mx_status_t emmc_sdhci_get_mmio(void* ctx, volatile sdhci_regs_t** out) {
    emmc_t* emmc = ctx;

    if (!emmc->mmio_base) {
        mx_status_t status = pdev_map_mmio(&emmc->pdev, MMIO_INDEX, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                           &emmc->mmio_base, &emmc->mmio_size, &emmc->mmio_handle);
        if (status != MX_OK) {
            return status;
        }
    }

    *out = emmc->mmio_base;
    return MX_OK;
}

static uint32_t emmc_sdhci_get_base_clock(void* ctx) {
    uint32_t base_clock = 0;
    emmc_t* emmc = ctx;
    bcm_bus_protocol_t bus_proto;
    mx_status_t st = pdev_get_protocol(&emmc->pdev, MX_PROTOCOL_BCM_BUS, (void*)&bus_proto);
    if (st != MX_OK) {
        xprintf("emmc: could not find MX_PROTOCOL_BCM_BUS\n");
        goto out;
    }
    const uint32_t bcm28xX_core_clock_id = 1;
    st = bcm_bus_get_clock_rate(&bus_proto, bcm28xX_core_clock_id, &base_clock);
     if (st < 0 || base_clock == 0) {
        xprintf("emmc: failed to get base clock rate, retcode = %d\n", st);
    }
out:
     return base_clock;
}

static mx_paddr_t emmc_sdhci_get_dma_offset(void* ctx) {
    return BCM_SDRAM_BUS_ADDR_BASE;
}

static uint64_t emmc_sdhci_get_quirks(void* ctx) {
    return SDHCI_QUIRK_STRIP_RESPONSE_CRC;
}

static sdhci_protocol_ops_t emmc_sdhci_proto = {
    .get_interrupt = emmc_sdhci_get_interrupt,
    .get_mmio = emmc_sdhci_get_mmio,
    .get_base_clock = emmc_sdhci_get_base_clock,
    .get_dma_offset = emmc_sdhci_get_dma_offset,
    .get_quirks = emmc_sdhci_get_quirks,
};

static void emmc_unbind(void* ctx) {
    emmc_t* emmc = ctx;
    device_remove(emmc->mxdev);
}

static void emmc_release(void* ctx) {
    emmc_t* emmc = ctx;

    if (emmc->mmio_base) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)emmc->mmio_base, emmc->mmio_size);
        mx_handle_close(emmc->mmio_handle);
    }
    free(emmc);
}

static mx_protocol_device_t emmc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = emmc_unbind,
    .release = emmc_release,
};

static mx_status_t emmc_bind(void* drv_ctx, mx_device_t* dev, void** cookie) {
    emmc_t* emmc = calloc(1, sizeof(emmc_t));
    if (!emmc) {
        return MX_ERR_NO_MEMORY;
    }
    mx_status_t st = device_get_protocol(dev, MX_PROTOCOL_PLATFORM_DEV, &emmc->pdev);
    if (st !=  MX_OK) {
        free(emmc);
        return st;
    }

    emmc->parent = dev;
    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bcm-emmc",
        .ctx = emmc,
        .ops = &emmc_device_proto,
        .proto_id = MX_PROTOCOL_SDHCI,
        .proto_ops = &emmc_sdhci_proto,
    };
    st = device_add(emmc->parent, &args, &emmc->mxdev);
    if (st != MX_OK) {
        goto fail;
    }
    return MX_OK;
fail:
    free(emmc);
    return st;
}

static mx_driver_ops_t emmc_dwc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = emmc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
MAGENTA_DRIVER_BEGIN(bcm_emmc, emmc_dwc_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BROADCOMM_EMMC),
MAGENTA_DRIVER_END(bcm_emmc)
// clang-format on
