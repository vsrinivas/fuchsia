// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/handle.h>

#include <ddk/metadata/emmc.h>
#include <ddk/metadata/gpt.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-emmc-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

static const std::vector<fpbus::Mmio> emmc_mmios{
    {{
        .base = T931_SD_EMMC_C_BASE,
        .length = T931_SD_EMMC_C_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> emmc_irqs{
    {{
        .irq = T931_SD_EMMC_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> emmc_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    }},
};

static aml_sdmmc_config_t sherlock_config = {
    .supports_dma = true,
    // As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .min_freq = 400'000,
    .max_freq = 166'666'667,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
    .use_new_tuning = true,
};

static aml_sdmmc_config_t luis_config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 166'666'667,  // The expected eMMC clock frequency on Luis is 166 MHz.
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

const emmc_config_t sherlock_emmc_config = {
    // Maintain the current Sherlock behavior until we determine that trim is needed.
    .enable_trim = false,
};

static const guid_map_t guid_map[] = {
    {"boot", GUID_ZIRCON_A_VALUE},
    {"system", GUID_ZIRCON_B_VALUE},
    {"recovery", GUID_ZIRCON_R_VALUE},
    {"cache", GUID_FVM_VALUE},
};

static_assert(sizeof(guid_map) / sizeof(guid_map[0]) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

static const std::vector<fpbus::Metadata> sherlock_emmc_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&sherlock_config),
            reinterpret_cast<const uint8_t*>(&sherlock_config) + sizeof(sherlock_config)),
    }},
    {{
        .type = DEVICE_METADATA_GUID_MAP,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&guid_map),
                                 reinterpret_cast<const uint8_t*>(&guid_map) + sizeof(guid_map)),
    }},
    {{
        .type = DEVICE_METADATA_EMMC_CONFIG,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&sherlock_emmc_config),
            reinterpret_cast<const uint8_t*>(&sherlock_emmc_config) + sizeof(sherlock_emmc_config)),
    }},
};

static const std::vector<fpbus::Metadata> luis_emmc_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&luis_config),
            reinterpret_cast<const uint8_t*>(&luis_config) + sizeof(luis_config)),
    }},
    {{
        .type = DEVICE_METADATA_GUID_MAP,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&guid_map),
                                 reinterpret_cast<const uint8_t*>(&guid_map) + sizeof(guid_map)),
    }},
};

static const std::vector<fpbus::BootMetadata> emmc_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    }},
};

}  // namespace

zx_status_t Sherlock::EmmcInit() {
  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(T931_EMMC_D0, T931_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D1, T931_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D2, T931_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D3, T931_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D4, T931_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D5, T931_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D6, T931_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D7, T931_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_CLK, T931_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_RST, T931_EMMC_RST_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_CMD, T931_EMMC_CMD_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_DS, T931_EMMC_DS_FN);

  gpio_impl_.SetDriveStrength(T931_EMMC_D0, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D1, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D2, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D3, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D4, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D5, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D6, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_D7, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_CLK, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_RST, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_CMD, 4000, nullptr);
  gpio_impl_.SetDriveStrength(T931_EMMC_DS, 4000, nullptr);

  gpio_impl_.ConfigIn(T931_EMMC_D0, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D1, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D2, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D3, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D4, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D5, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D6, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_D7, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_CLK, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_RST, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_CMD, GPIO_PULL_UP);
  gpio_impl_.ConfigIn(T931_EMMC_DS, GPIO_PULL_DOWN);

  fpbus::Node emmc_dev;
  emmc_dev.name() = "sherlock-emmc";
  emmc_dev.vid() = PDEV_VID_AMLOGIC;
  emmc_dev.pid() = PDEV_PID_GENERIC;
  emmc_dev.did() = PDEV_DID_AMLOGIC_SDMMC_C;
  emmc_dev.mmio() = emmc_mmios;
  emmc_dev.irq() = emmc_irqs;
  emmc_dev.bti() = emmc_btis;
  emmc_dev.metadata() = sherlock_emmc_metadata;
  emmc_dev.boot_metadata() = emmc_boot_metadata;

  if (pid_ == PDEV_PID_LUIS) {
    emmc_dev.metadata() = luis_emmc_metadata;
  }

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('EMMC');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, emmc_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, sherlock_emmc_fragments,
                                               std::size(sherlock_emmc_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Emmc(emmc_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Emmc(emmc_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
