// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/nand.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <soc/as370/as370-nand.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::NandInit() {
  constexpr pbus_mmio_t nand_mmios[] = {
      {.base = as370::kNandBase, .length = as370::kNandSize},
      {.base = as370::kNandFifoBase, .length = as370::kNandFifoSize},
  };

  constexpr pbus_irq_t nand_irqs[] = {
      {
          .irq = as370::kNandIrq,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  constexpr nand_config_t nand_config = {
      .bad_block_config = {.type = kSynaptics,
                           .synaptics = {.table_start_block = 2044, .table_end_block = 2047}},
      .extra_partition_config_count = 0,
      .extra_partition_config = {}};

  constexpr zbi_partition_t kPartitions[] = {
      // The first nine blocks are only accessed with ECC disabled.
      // {{},                        {},    0,    0, 0, "block0"},
      // {{},                        {},    1,    8, 0, "prebootloader"},
      {{}, {}, 9, 40, 0, "tzk_normal"},
      {{}, {}, 41, 72, 0, "tzk_normalB"},
      {GUID_BOOTLOADER_VALUE, {}, 73, 76, 0, "bl_normal"},
      {GUID_BOOTLOADER_VALUE, {}, 77, 80, 0, "bl_normalB"},
      {GUID_ZIRCON_A_VALUE, {}, 81, 144, 0, "boot"},
      {GUID_ZIRCON_B_VALUE, {}, 145, 208, 0, "bootB"},
      {GUID_FVM_VALUE, {}, 209, 1923, 0, "fvm"},
      {GUID_ZIRCON_R_VALUE, {}, 1924, 1975, 0, "recovery"},
      {{}, {}, 1976, 1979, 0, "fts"},
      {GUID_FACTORY_CONFIG_VALUE, {}, 1980, 1991, 0, "factory_store"},
      {{}, {}, 1992, 1995, 0, "key_1st"},
      {{}, {}, 1996, 1999, 0, "key_2nd"},
      {{}, {}, 2000, 2019, 0, "fastboot_1st"},
      {{}, {}, 2020, 2039, 0, "fastboot_2nd"},
  };

  constexpr size_t kPartitionMapAlignment =
      alignof(zbi_partition_map_t) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
          ? alignof(zbi_partition_map_t)
          : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
  constexpr size_t kPartitionMapSize = sizeof(zbi_partition_map_t) + sizeof(kPartitions);

  std::unique_ptr<zbi_partition_map_t> nand_partition_map(
      reinterpret_cast<zbi_partition_map_t*>(aligned_alloc(
          kPartitionMapAlignment, fbl::round_up(kPartitionMapSize, kPartitionMapAlignment))));
  if (!nand_partition_map) {
    return ZX_ERR_NO_MEMORY;
  }

  nand_partition_map->block_count = 2048;
  nand_partition_map->block_size = 4096 * 64;
  nand_partition_map->partition_count = std::size(kPartitions);
  nand_partition_map->reserved = 0;
  memset(nand_partition_map->guid, 0, sizeof(nand_partition_map->guid));
  memcpy(nand_partition_map->partitions, kPartitions, sizeof(kPartitions));

  const pbus_metadata_t nand_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE,
       .data_buffer = &nand_config,
       .data_size = sizeof(nand_config)},
      {.type = DEVICE_METADATA_PARTITION_MAP,
       .data_buffer = nand_partition_map.get(),
       .data_size = kPartitionMapSize},
  };

  pbus_dev_t nand_dev = {};
  nand_dev.name = "nand";
  nand_dev.vid = PDEV_VID_GENERIC;
  nand_dev.pid = PDEV_PID_GENERIC;
  nand_dev.did = PDEV_DID_CADENCE_HPNFC;
  nand_dev.mmio_list = nand_mmios;
  nand_dev.mmio_count = countof(nand_mmios);
  nand_dev.irq_list = nand_irqs;
  nand_dev.irq_count = countof(nand_irqs);
  nand_dev.metadata_list = nand_metadata;
  nand_dev.metadata_count = countof(nand_metadata);

  zx_status_t status = pbus_.DeviceAdd(&nand_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
