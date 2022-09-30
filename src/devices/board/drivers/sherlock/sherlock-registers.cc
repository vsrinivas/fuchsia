// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-registers.h>

#include "sherlock.h"
#include "src/devices/lib/metadata/llcpp/registers.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

enum MmioMetadataIdx {
  RESET_MMIO,
#ifdef FACTORY_BUILD
  USB_FACTORY_MMIO,
#endif  // FACTORY_BUILD

  MMIO_COUNT,
};

}  // namespace

zx_status_t Sherlock::RegistersInit() {
  const std::vector<fpbus::Mmio> registers_mmios = {
      {{
          .base = T931_RESET_BASE,
          .length = T931_RESET_LENGTH,
      }},
#ifdef FACTORY_BUILD
      []() {
        fpbus::Mmio ret;
        ret.base() = T931_USB_BASE;
        ret.length() = T931_USB_LENGTH;
        return ret;
      }(),
#endif  // FACTORY_BUILD
  };    // namespace sherlock

  fidl::Arena<2048> allocator;
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
                                       .mmio_offset = T931_RESET1_REGISTER,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::USB_RESET1_LEVEL_MASK,
                                       .mmio_offset = T931_RESET1_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  register_entries[aml_registers::REGISTER_NNA_RESET_LEVEL2] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_NNA_RESET_LEVEL2, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::NNA_RESET2_LEVEL_MASK,
                                       .mmio_offset = T931_RESET2_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  register_entries[aml_registers::REGISTER_MALI_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_MALI_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = T931_RESET0_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET0_MASK,
                                       .mmio_offset = T931_RESET0_LEVEL,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = T931_RESET2_MASK,
                                       .reg_count = 1,
                                   },
                                   {
                                       .mask = aml_registers::MALI_RESET2_MASK,
                                       .mmio_offset = T931_RESET2_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  register_entries[aml_registers::REGISTER_ISP_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_ISP_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::ISP_RESET4_MASK,
                                       .mmio_offset = T931_RESET4_LEVEL,
                                       .reg_count = 1,
                                   },
                               });

  register_entries[aml_registers::REGISTER_SPICC0_RESET] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_SPICC0_RESET, RESET_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = aml_registers::SPICC0_RESET_MASK,
                                       .mmio_offset = T931_RESET6_REGISTER,
                                       .reg_count = 1,
                                   },
                               });

#ifdef FACTORY_BUILD
  mmio_entries[USB_FACTORY_MMIO] = registers::BuildMetadata(allocator, USB_FACTORY_MMIO);

  register_entries[aml_registers::REGISTER_USB_PHY_FACTORY] =
      registers::BuildMetadata(allocator, aml_registers::REGISTER_USB_PHY_FACTORY, USB_FACTORY_MMIO,
                               std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                   {
                                       .mask = 0xFFFFFFFF,
                                       .mmio_offset = 0,
                                       .reg_count = T931_USB_LENGTH / sizeof(uint32_t),
                                       .overlap_check_on = false,
                                   },
                               });
#endif  // FACTORY_BUILD

  auto metadata =
      registers::BuildMetadata(allocator, std::move(mmio_entries), std::move(register_entries));
  fidl::unstable::OwnedEncodedMessage<registers::Metadata> encoded_metadata(
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

  fpbus::Node registers_dev;
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

}  // namespace sherlock
