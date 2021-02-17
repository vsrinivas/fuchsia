// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/aml-common/aml-registers.h>

#include "src/devices/lib/metadata/llcpp/registers.h"
#include "vim3.h"

namespace vim3 {

namespace {

enum MmioMetadataIdx {
  RESET_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t Vim3::RegistersInit() {
  static const pbus_mmio_t registers_mmios[] = {
      {
          .base = A311D_RESET_BASE,
          .length = A311D_RESET_LENGTH,
      },
  };

  fidl::FidlAllocator<2048> allocator;
  fidl::VectorView<registers::MmioMetadataEntry> mmio_entries(allocator, MMIO_COUNT);

  mmio_entries[RESET_MMIO] = registers::BuildMetadata(allocator, RESET_MMIO);

  fidl::VectorView<registers::RegistersMetadataEntry> register_entries(
      allocator, aml_registers::REGISTER_ID_COUNT);

  register_entries[aml_registers::REGISTER_USB_PHY_V2_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_USB_PHY_V2_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::USB_RESET1_REGISTER_UNKNOWN_1_MASK |
                                               aml_registers::USB_RESET1_REGISTER_UNKNOWN_2_MASK,
                                       .mmio_offset = A311D_RESET1_REGISTER,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::USB_RESET1_LEVEL_MASK,
                                       .mmio_offset = A311D_RESET1_LEVEL,
                                       .reg_count = 1,
                                   },
                               });
  register_entries[aml_registers::REGISTER_NNA_RESET_LEVEL2] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_NNA_RESET_LEVEL2, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::NNA_RESET2_LEVEL_MASK,
                                       .mmio_offset = A311D_RESET2_LEVEL,
                                       .reg_count = 1,
                                   },
                               });
  register_entries[aml_registers::REGISTER_MALI_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_MALI_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = A311D_RESET0_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = A311D_RESET0_LEVEL,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = A311D_RESET2_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = A311D_RESET2_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  auto metadata =
      registers::BuildMetadata(allocator, std::move(mmio_entries), std::move(register_entries));
  fidl::OwnedEncodedMessage<registers::Metadata> encoded_metadata(&metadata);
  if (!encoded_metadata.ok() || (encoded_metadata.error() != nullptr)) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__, encoded_metadata.error());
    return encoded_metadata.status();
  }

  static const pbus_metadata_t registers_metadata[] = {
      {
          .type = DEVICE_METADATA_REGISTERS,
          .data_buffer = encoded_metadata.GetOutgoingMessage().bytes(),
          .data_size = encoded_metadata.GetOutgoingMessage().byte_actual(),
      },
  };

  static pbus_dev_t registers_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "registers";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_REGISTERS;
    dev.mmio_list = registers_mmios;
    dev.mmio_count = countof(registers_mmios);
    dev.metadata_list = registers_metadata;
    dev.metadata_count = countof(registers_metadata);
    return dev;
  }();

  zx_status_t status = pbus_.DeviceAdd(&registers_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
