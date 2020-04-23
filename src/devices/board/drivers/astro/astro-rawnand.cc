// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <zircon/hw/gpt.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/metadata/nand.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/bus.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-guid.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t raw_nand_mmios[] = {
    {
        /* nandreg : Registers for NAND controller */
        .base = S905D2_RAW_NAND_REG_BASE,
        .length = 0x2000,
    },
    {
        /* clockreg : Clock Register for NAND controller */
        .base = S905D2_RAW_NAND_CLOCK_BASE,
        .length = 0x4, /* Just 4 bytes */
    },
};

static const pbus_irq_t raw_nand_irqs[] = {
    {
        .irq = S905D2_RAW_NAND_IRQ,
        .mode = 0,
    },
};

static const pbus_bti_t raw_nand_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AML_RAW_NAND,
    },
};

static const nand_config_t config = {
    .bad_block_config =
        {
            .type = kAmlogicUboot,
            .aml_uboot =
                {
                    .table_start_block = 20,
                    .table_end_block = 23,
                },
        },
    .extra_partition_config_count = 3,
    .extra_partition_config =
        {
            {
                .type_guid = GUID_BL2_VALUE,
                .copy_count = 8,
                .copy_byte_offset = 0,
            },
            {
                .type_guid = GUID_BOOTLOADER_VALUE,
                .copy_count = 4,
                .copy_byte_offset = 0,
            },
            {
                .type_guid = GUID_SYS_CONFIG_VALUE,
                .copy_count = 4,
                .copy_byte_offset = 0,
            },

        },
};

static const pbus_metadata_t raw_nand_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &config,
        .data_size = sizeof(config),
    },
};

static const pbus_boot_metadata_t raw_nand_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    },
};

static const pbus_dev_t raw_nand_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "raw_nand";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_RAW_NAND;
  dev.mmio_list = raw_nand_mmios;
  dev.mmio_count = countof(raw_nand_mmios);
  dev.irq_list = raw_nand_irqs;
  dev.irq_count = countof(raw_nand_irqs);
  dev.bti_list = raw_nand_btis;
  dev.bti_count = countof(raw_nand_btis);
  dev.metadata_list = raw_nand_metadata;
  dev.metadata_count = countof(raw_nand_metadata);
  dev.boot_metadata_list = raw_nand_boot_metadata;
  dev.boot_metadata_count = countof(raw_nand_boot_metadata);
  return dev;
}();

zx_status_t Astro::RawNandInit() {
  zx_status_t status;

  // Set alternate functions to enable raw_nand.
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(8), 2);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(9), 2);
  if (status != ZX_OK) {
    return status;
  }
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(10), 2);
  if (status != ZX_OK) {
    return status;
  }
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(11), 2);
  if (status != ZX_OK) {
    return status;
  }
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(12), 2);
  if (status != ZX_OK) {
    return status;
  }
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(14), 2);
  if (status != ZX_OK) {
    return status;
  }
  status = gpio_impl_.SetAltFunction(S905D2_GPIOBOOT(15), 2);
  if (status != ZX_OK) {
    return status;
  }

  status = pbus_.DeviceAdd(&raw_nand_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
