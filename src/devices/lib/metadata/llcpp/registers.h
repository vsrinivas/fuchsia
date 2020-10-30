// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
#define SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_

#include <fuchsia/hardware/registers/llcpp/fidl.h>

namespace registers {

using ::llcpp::fuchsia::hardware::registers::Mask;
template <typename T>
Mask BuildMask(fidl::Allocator& allocator, T mask) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return Mask::WithR8(allocator.make<uint8_t>(mask));
  }
  if constexpr (std::is_same_v<T, uint16_t>) {
    return Mask::WithR16(allocator.make<uint16_t>(mask));
  }
  if constexpr (std::is_same_v<T, uint32_t>) {
    return Mask::WithR32(allocator.make<uint32_t>(mask));
  }
  if constexpr (std::is_same_v<T, uint64_t>) {
    return Mask::WithR64(allocator.make<uint64_t>(mask));
  }
  return Mask();
}

using ::llcpp::fuchsia::hardware::registers::MaskEntry;
using ::llcpp::fuchsia::hardware::registers::RegistersMetadataEntry;
template <typename T>
RegistersMetadataEntry BuildMetadata(fidl::Allocator& allocator, uint64_t id, uint64_t base_address,
                                     std::vector<std::pair<T, uint32_t>> masks) {
  fidl::VectorView<MaskEntry> built_masks;
  built_masks.set_data(allocator.make<MaskEntry[]>(masks.size()));
  built_masks.set_count(masks.size());
  for (uint32_t i = 0; i < masks.size(); i++) {
    built_masks[i] =
        MaskEntry::Builder(allocator.make<MaskEntry::Frame>())
            .set_count(allocator.make<uint32_t>(masks[i].second))
            .set_mask(allocator.make<Mask>(BuildMask<T>(allocator, masks[i].first)))
            .build();
  }

  return RegistersMetadataEntry::Builder(allocator.make<RegistersMetadataEntry::Frame>())
      .set_id(allocator.make<uint64_t>(id))
      .set_base_address(allocator.make<uint64_t>(base_address))
      .set_masks(allocator.make<fidl::VectorView<MaskEntry>>(std::move(built_masks)))
      .build();
}

using ::llcpp::fuchsia::hardware::registers::MmioMetadataEntry;
MmioMetadataEntry BuildMetadata(fidl::Allocator& allocator, uint64_t base_address) {
  return MmioMetadataEntry::Builder(allocator.make<MmioMetadataEntry::Frame>())
      .set_base_address(allocator.make<uint64_t>(base_address))
      .build();
}

using ::llcpp::fuchsia::hardware::registers::Metadata;
Metadata BuildMetadata(fidl::Allocator& allocator, fidl::VectorView<MmioMetadataEntry> mmio,
                       fidl::VectorView<RegistersMetadataEntry> registers) {
  return Metadata::Builder(allocator.make<Metadata::Frame>())
      .set_mmio(allocator.make<fidl::VectorView<MmioMetadataEntry>>(std::move(mmio)))
      .set_registers(allocator.make<fidl::VectorView<RegistersMetadataEntry>>(std::move(registers)))
      .build();
}

}  // namespace registers

#endif  // SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
