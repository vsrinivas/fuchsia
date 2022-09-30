// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <unistd.h>
#include <zircon/hw/gpt.h>

#include <ddk/metadata/nand.h>
#include <soc/aml-common/aml-guid.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> raw_nand_mmios{
    {{
        /* nandreg : Registers for NAND controller */
        .base = S905D2_RAW_NAND_REG_BASE,
        .length = 0x2000,
    }},
    {{
        /* clockreg : Clock Register for NAND controller */
        .base = S905D2_RAW_NAND_CLOCK_BASE,
        .length = 0x4,
    }},
};

static const std::vector<fpbus::Irq> raw_nand_irqs{
    {{
        .irq = S905D2_RAW_NAND_IRQ,
        .mode = 0,
    }},
};

static const std::vector<fpbus::Bti> raw_nand_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_AML_RAW_NAND,
    }},
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

static const std::vector<fpbus::Metadata> raw_nand_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&config),
                                     reinterpret_cast<const uint8_t*>(&config) + sizeof(config)),
    }},
};

static const std::vector<fpbus::BootMetadata> raw_nand_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    }},
};

static const fpbus::Node raw_nand_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "raw_nand";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_RAW_NAND;
  dev.mmio() = raw_nand_mmios;
  dev.irq() = raw_nand_irqs;
  dev.bti() = raw_nand_btis;
  dev.metadata() = raw_nand_metadata;
  dev.boot_metadata() = raw_nand_boot_metadata;
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

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('RAWN');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, raw_nand_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd RawNand(raw_nand_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd RawNand(raw_nand_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace astro
