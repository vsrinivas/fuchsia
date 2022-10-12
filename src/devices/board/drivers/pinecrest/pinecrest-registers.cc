// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-hw.h>

#include "pinecrest.h"
#include "src/devices/lib/as370/include/soc/as370/as370-nna.h"
#include "src/devices/lib/metadata/llcpp/registers.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

enum MmioMetadataIdx {
  GBL_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t Pinecrest::RegistersInit() {
  static const std::vector<fpbus::Mmio> registers_mmios{
      []() {
        fpbus::Mmio ret;
        ret.base() = as370::kGlobalBase;
        ret.length() = as370::kGlobalSize;
        return ret;
      }(),
  };

  fidl::Arena allocator;
  fidl::VectorView<registers::MmioMetadataEntry> mmio_entries(allocator, MMIO_COUNT);

  mmio_entries[GBL_MMIO] = registers::BuildMetadata(allocator, GBL_MMIO);

  fidl::VectorView<registers::RegistersMetadataEntry> register_entries(allocator, 1);

  register_entries[0] = registers::BuildMetadata(allocator, 0, GBL_MMIO,
                                                 std::vector<registers::MaskEntryBuilder<uint32_t>>{
                                                     {
                                                         .mask = as370::kNnaPowerMask,
                                                         .mmio_offset = as370::kNnaPowerOffset,
                                                         .reg_count = 1,
                                                     },
                                                     {
                                                         .mask = as370::kNnaResetMask,
                                                         .mmio_offset = as370::kNnaResetOffset,
                                                         .reg_count = 1,
                                                     },
                                                     {
                                                         .mask = as370::kNnaClockSysMask,
                                                         .mmio_offset = as370::kNnaClockSysOffset,
                                                         .reg_count = 1,
                                                     },
                                                     {
                                                         .mask = as370::kNnaClockCoreMask,
                                                         .mmio_offset = as370::kNnaClockCoreOffset,
                                                         .reg_count = 1,
                                                     },
                                                 });
  auto metadata =
      registers::BuildMetadata(allocator, std::move(mmio_entries), std::move(register_entries));
  fidl::unstable::OwnedEncodedMessage<registers::Metadata> encoded_metadata(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "Could not build metadata %s", encoded_metadata.FormatDescription().c_str());
    return encoded_metadata.status();
  }

  auto encoded_metadata_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
  std::vector<fpbus::Metadata> registers_metadata{
      [&]() {
        fpbus::Metadata ret;
        ret.type() = DEVICE_METADATA_REGISTERS;
        ret.data() = std::vector(encoded_metadata_bytes.data(),
                                 encoded_metadata_bytes.data() + encoded_metadata_bytes.size());
        return ret;
      }(),
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
    zxlogf(ERROR, "%s: DeviceAdd Registers request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Registers failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}
}  // namespace board_pinecrest
