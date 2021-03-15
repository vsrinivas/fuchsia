// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <fbl/algorithm.h>
#include <soc/mt8167/mt8167-sdmmc.h>
#include <soc/mt8183/mt8183-hw.h>

#include "c18.h"
#include "src/devices/board/drivers/c18/c18_bind.h"

namespace {

constexpr uint32_t kFifoDepth = 128;
constexpr uint32_t kSrcClkFreq = 416000000;

}  // namespace

namespace board_c18 {

zx_status_t C18::Msdc0Init() {
  zx_status_t status;

  static const pbus_mmio_t msdc0_mmios[] = {{
      .base = MT8183_MSDC0_BASE,
      .length = MT8183_MSDC0_SIZE,
  }};

  static const pbus_bti_t msdc0_btis[] = {{
      .iommu_index = 0,
      .bti_id = BTI_MSDC0,
  }};

  static const board_mt8167::MtkSdmmcConfig msdc0_config = {
      .fifo_depth = kFifoDepth, .src_clk_freq = kSrcClkFreq, .is_sdio = false};

  static const guid_map_t guid_map[] = {
      // Mappings for ChromeOS paritition names for C18/C19.
      {"STATE", GUID_LINUX_FILESYSTEM_DATA_VALUE}, {"KERN-A", GUID_CROS_KERNEL_VALUE},
      {"ROOT-A", GUID_CROS_ROOTFS_VALUE},          {"KERN-B", GUID_CROS_KERNEL_VALUE},
      {"ROOT-B", GUID_CROS_ROOTFS_VALUE},          {"KERN-C", GUID_CROS_KERNEL_VALUE},
      {"ROOT-C", GUID_CROS_ROOTFS_VALUE},          {"OEM", GUID_LINUX_FILESYSTEM_DATA_VALUE},
      {"reserved", GUID_CROS_RESERVED_NAME},       {"reserved", GUID_CROS_RESERVED_NAME},
      {"RWFW", GUID_CROS_FIRMWARE_VALUE},          {"EFI-SYSTEM", GUID_EFI_VALUE},
  };
  static_assert(std::size(guid_map) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

  static const pbus_metadata_t msdc0_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE,
       .data_buffer = &msdc0_config,
       .data_size = sizeof(msdc0_config)},
      {.type = DEVICE_METADATA_GUID_MAP, .data_buffer = guid_map, .data_size = sizeof(guid_map)}};

  static const pbus_irq_t msdc0_irqs[] = {
      {.irq = MT8183_IRQ_MSDC0, .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH}};

  pbus_dev_t msdc0_dev = {};
  msdc0_dev.name = "emmc";
  msdc0_dev.vid = PDEV_VID_MEDIATEK;
  msdc0_dev.did = PDEV_DID_MEDIATEK_MSDC0;
  msdc0_dev.mmio_list = msdc0_mmios;
  msdc0_dev.mmio_count = countof(msdc0_mmios);
  msdc0_dev.bti_list = msdc0_btis;
  msdc0_dev.bti_count = countof(msdc0_btis);
  msdc0_dev.metadata_list = msdc0_metadata;
  msdc0_dev.metadata_count = countof(msdc0_metadata);
  msdc0_dev.irq_list = msdc0_irqs;
  msdc0_dev.irq_count = countof(msdc0_irqs);

  static constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  static constexpr zx_bind_inst_t reset_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8183_GPIO_MSDC0_RST),
  };
  static const device_fragment_part_t reset_gpio_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(reset_gpio_match), reset_gpio_match},
  };
  static const device_fragment_t fragments[] = {
      {"gpio-reset", std::size(reset_gpio_fragment), reset_gpio_fragment},
  };

  status = pbus_.CompositeDeviceAdd(&msdc0_dev, reinterpret_cast<uint64_t>(fragments),
                                    std::size(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd MSDC0 failed: %d", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_c18
