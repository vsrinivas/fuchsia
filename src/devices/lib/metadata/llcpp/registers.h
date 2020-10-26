// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
#define SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_

#include <fuchsia/hardware/registers/llcpp/fidl.h>

namespace registers {

using ::llcpp::fuchsia::hardware::registers::Mask;
template <typename T>
Mask BuildMask(T mask) {
  if (std::is_same_v<T, uint8_t>) {
    return Mask::WithR8(std::make_unique<uint8_t>(mask));
  }
  if (std::is_same_v<T, uint16_t>) {
    return Mask::WithR16(std::make_unique<uint16_t>(mask));
  }
  if (std::is_same_v<T, uint32_t>) {
    return Mask::WithR32(std::make_unique<uint32_t>(mask));
  }
  if (std::is_same_v<T, uint64_t>) {
    return Mask::WithR64(std::make_unique<uint64_t>(mask));
  }
  return Mask();
}

using ::llcpp::fuchsia::hardware::registers::MaskEntry;
using ::llcpp::fuchsia::hardware::registers::RegistersMetadataEntry;
template <typename T>
RegistersMetadataEntry BuildMetadata(uint64_t id, uint64_t base_address,
                                     std::vector<std::pair<T, uint32_t>> masks) {
  auto built_masks = std::make_unique<MaskEntry[]>(masks.size());
  for (uint32_t i = 0; i < masks.size(); i++) {
    built_masks[i] = MaskEntry::Builder(std::make_unique<MaskEntry::Frame>())
                         .set_count(std::make_unique<uint32_t>(masks[i].second))
                         .set_mask(std::make_unique<Mask>(std::move(BuildMask<T>(masks[i].first))))
                         .build();
  }

  return RegistersMetadataEntry::Builder(std::make_unique<RegistersMetadataEntry::Frame>())
      .set_id(std::make_unique<uint64_t>(id))
      .set_base_address(std::make_unique<uint64_t>(base_address))
      .set_masks(
          std::make_unique<fidl::VectorView<MaskEntry>>(std::move(built_masks), masks.size()))
      .build();
}

using ::llcpp::fuchsia::hardware::registers::MmioMetadataEntry;
MmioMetadataEntry BuildMetadata(uint64_t base_address) {
  return MmioMetadataEntry::Builder(std::make_unique<MmioMetadataEntry::Frame>())
      .set_base_address(std::make_unique<uint64_t>(base_address))
      .build();
}

using ::llcpp::fuchsia::hardware::registers::Metadata;
Metadata BuildMetadata(std::unique_ptr<MmioMetadataEntry[]> mmio, uint64_t mmio_count,
                       std::unique_ptr<RegistersMetadataEntry[]> registers,
                       uint64_t registers_count) {
  return Metadata::Builder(std::make_unique<Metadata::Frame>())
      .set_mmio(std::make_unique<fidl::VectorView<MmioMetadataEntry>>(std::move(mmio), mmio_count))
      .set_registers(std::make_unique<fidl::VectorView<RegistersMetadataEntry>>(
          std::move(registers), registers_count))
      .build();
}

}  // namespace registers

#endif  // SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
