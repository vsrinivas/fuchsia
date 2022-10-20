// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-registers.h>

#include "buckeye.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

enum MmioMetadataIdx {
  RESET_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t Buckeye::RegistersInit() {
  static const std::vector<fpbus::Mmio> registers_mmios{
      {{
          .base = A5_RESET_BASE,
          .length = A5_RESET_LENGTH,
      }},
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
  std::vector<fpbus::Metadata> registers_metadata{
      {{
          .type = DEVICE_METADATA_REGISTERS,
          .data =
              std::vector<uint8_t>(encoded_metadata_bytes.data(),
                                   encoded_metadata_bytes.data() + encoded_metadata_bytes.size()),
      }},
  };

  fpbus::Node registers_dev = {};
  registers_dev.name() = "registers";
  registers_dev.vid() = PDEV_VID_GENERIC;
  registers_dev.pid() = PDEV_PID_GENERIC;
  registers_dev.did() = PDEV_DID_REGISTERS;
  registers_dev.mmio() = registers_mmios;
  registers_dev.metadata() = registers_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('REGI');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, registers_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Registers(registers_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Registers(registers_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace buckeye
