// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <memory>

#include <ddk/metadata/nand.h>
#include <fbl/alloc_checker.h>
#include <soc/as370/as370-nand.h>

#include "pinecrest.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::NandInit() {
  static const std::vector<fpbus::Mmio> nand_mmios{
      {{
          .base = as370::kNandBase,
          .length = as370::kNandSize,
      }},
      {{
          .base = as370::kNandFifoBase,
          .length = as370::kNandFifoSize,
      }},
  };

  static const std::vector<fpbus::Irq> nand_irqs{
      {{
          .irq = as370::kNandIrq,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      }},
  };

  constexpr nand_config_t nand_config = {
      .bad_block_config = {.type = kSynaptics,
                           .synaptics = {.table_start_block = 2044, .table_end_block = 2047}},
      .extra_partition_config_count = 0,
      .extra_partition_config = {}};

  // TODO(fxbug.dev/104572): This layout is not final and may change in the future.
  constexpr zbi_partition_t kPartitions[] = {
      // The first nine blocks are only accessed with ECC disabled.
      // {{},                        {},    0,    0, 0, "block0"},
      // {{},                        {},    1,    8, 0, "prebootloader"},
      {{}, {}, 9, 40, 0, "tzk_normal"},
      {{}, {}, 41, 72, 0, "tzk_normalB"},
      {GUID_BOOTLOADER_VALUE, {}, 73, 76, 0, "bl_normal"},
      {GUID_BOOTLOADER_VALUE, {}, 77, 80, 0, "bl_normalB"},
      {GUID_ZIRCON_A_VALUE, {}, 81, 144, 0, "boot"},
      {GUID_ZIRCON_R_VALUE, {}, 145, 208, 0, "bootB"},
      {GUID_FVM_VALUE, {}, 209, 1923, 0, "fvm"},
      {{}, {}, 1924, 1975, 0, "recovery"},
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

  std::vector<fpbus::Metadata> nand_metadata{
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&nand_config),
              reinterpret_cast<const uint8_t*>(&nand_config) + sizeof(nand_config)),
      }},
      {{
          .type = DEVICE_METADATA_PARTITION_MAP,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(nand_partition_map.get()),
              reinterpret_cast<const uint8_t*>(nand_partition_map.get()) + kPartitionMapSize),
      }},
  };

  fpbus::Node nand_dev;
  nand_dev.name() = "nand";
  nand_dev.vid() = PDEV_VID_GENERIC;
  nand_dev.pid() = PDEV_PID_GENERIC;
  nand_dev.did() = PDEV_DID_CADENCE_HPNFC;
  nand_dev.mmio() = nand_mmios;
  nand_dev.irq() = nand_irqs;
  nand_dev.metadata() = nand_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('NAND');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, nand_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Nand(nand_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Nand(nand_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_pinecrest
