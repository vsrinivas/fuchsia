// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-registers.h>

#include "av400.h"

namespace av400 {

namespace {

enum MmioMetadataIdx {
  RESET_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t Av400::RegistersInit() {
  static const pbus_mmio_t registers_mmios[] = {
      {
          .base = A5_RESET_BASE,
          .length = A5_RESET_LENGTH,
      },
  };

  fidl::Arena<2048> allocator;

  fidl::VectorView<fuchsia_hardware_registers::wire::MmioMetadataEntry> mmio_entries(allocator,
                                                                                     MMIO_COUNT);
  mmio_entries[RESET_MMIO] = fuchsia_hardware_registers::wire::MmioMetadataEntry::Builder(allocator)
                                 .id(RESET_MMIO)
                                 .Build();

  fidl::VectorView<fuchsia_hardware_registers::wire::MaskEntry> built_masks(allocator, 1);
  auto mask_item =
      fuchsia_hardware_registers::wire::Mask::WithR32(aml_registers::A5_NNA_RESET1_LEVEL_MASK);
  built_masks[0] = fuchsia_hardware_registers::wire::MaskEntry::Builder(allocator)
                       .mask(mask_item)
                       .mmio_offset(A5_RESET1_LEVEL)
                       .count(1)
                       .overlap_check_on(true)
                       .Build();

  fidl::VectorView<fuchsia_hardware_registers::wire::RegistersMetadataEntry> register_entries(
      allocator, aml_registers::REGISTER_ID_COUNT);
  register_entries[aml_registers::REGISTER_NNA_RESET_LEVEL2] =
      fuchsia_hardware_registers::wire::RegistersMetadataEntry::Builder(allocator)
          .bind_id(aml_registers::REGISTER_NNA_RESET_LEVEL2)
          .mmio_id(RESET_MMIO)
          .masks(built_masks)
          .Build();

  fidl::VectorView<fuchsia_hardware_registers::wire::MaskEntry> usb_masks(allocator, 2);
  auto mask_item1 =
      fuchsia_hardware_registers::wire::Mask::WithR32(aml_registers::A5_USB_RESET0_MASK);
  usb_masks[0] = fuchsia_hardware_registers::wire::MaskEntry::Builder(allocator)
                     .mask(mask_item1)
                     .mmio_offset(A5_RESET0_REGISTER)
                     .count(1)
                     .overlap_check_on(true)
                     .Build();

  auto mask_item2 =
      fuchsia_hardware_registers::wire::Mask::WithR32(aml_registers::A5_USB_RESET0_LEVEL_MASK);
  usb_masks[1] = fuchsia_hardware_registers::wire::MaskEntry::Builder(allocator)
                     .mask(mask_item2)
                     .mmio_offset(A5_RESET0_LEVEL)
                     .count(1)
                     .overlap_check_on(true)
                     .Build();

  register_entries[aml_registers::REGISTER_USB_PHY_V2_RESET] =
      fuchsia_hardware_registers::wire::RegistersMetadataEntry::Builder(allocator)
          .bind_id(aml_registers::REGISTER_USB_PHY_V2_RESET)
          .mmio_id(RESET_MMIO)
          .masks(usb_masks)
          .Build();

  auto metadata = fuchsia_hardware_registers::wire::Metadata::Builder(allocator)
                      .mmio(mmio_entries)
                      .registers(register_entries)
                      .Build();

  fidl::unstable::OwnedEncodedMessage<fuchsia_hardware_registers::wire::Metadata> encoded_metadata(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__,
           encoded_metadata.FormatDescription().c_str());
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
    dev.mmio_count = std::size(registers_mmios);
    dev.metadata_list = registers_metadata;
    dev.metadata_count = std::size(registers_metadata);
    return dev;
  }();

  zx_status_t status = pbus_.DeviceAdd(&registers_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
