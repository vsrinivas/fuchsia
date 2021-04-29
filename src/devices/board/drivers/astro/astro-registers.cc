// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <memory>
#include <vector>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"
#include "src/devices/lib/metadata/llcpp/registers.h"

namespace astro {

namespace {

enum MmioMetadataIdx {
  RESET_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t Astro::RegistersInit() {
  static const pbus_mmio_t registers_mmios[] = {
      {
          .base = S905D2_RESET_BASE,
          .length = S905D2_RESET_LENGTH,
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
                                       .mmio_offset = S905D2_RESET1_REGISTER,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::USB_RESET1_LEVEL_MASK,
                                       .mmio_offset = S905D2_RESET1_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  register_entries[aml_registers::REGISTER_MALI_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_MALI_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = S905D2_RESET0_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = S905D2_RESET0_LEVEL,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = S905D2_RESET2_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = S905D2_RESET2_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  auto metadata =
      registers::BuildMetadata(allocator, std::move(mmio_entries), std::move(register_entries));
  fidl::OwnedEncodedMessage<registers::Metadata> encoded_metadata(&metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__, encoded_metadata.error_message());
    return encoded_metadata.status();
  }

  auto encoded_metadata_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
  static const pbus_metadata_t registers_metadata[] = {
      {
          .type = DEVICE_METADATA_REGISTERS,
          .data_buffer = encoded_metadata_bytes.data(),
          .data_size = encoded_metadata_bytes.size(),
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

}  // namespace astro
