// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-hw.h>

#include "as370.h"
#include "src/devices/lib/as370/include/soc/as370/as370-nna.h"
#include "src/devices/lib/metadata/llcpp/registers.h"

namespace board_as370 {

namespace {

enum MmioMetadataIdx {
  GBL_MMIO,

  MMIO_COUNT,
};

}  // namespace

zx_status_t As370::RegistersInit() {
  static const pbus_mmio_t registers_mmios[] = {
      {
          .base = as370::kGlobalBase,
          .length = as370::kGlobalSize,
      },
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
    zxlogf(ERROR, "DeviceAdd failed %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}
}  // namespace board_as370
