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

template <typename T>
struct MaskEntryBuilder {
  T mask;
  uint64_t mmio_offset;
  uint32_t reg_count;
  bool overlap_check_on = true;
};
using ::llcpp::fuchsia::hardware::registers::MaskEntry;
using ::llcpp::fuchsia::hardware::registers::RegistersMetadataEntry;
template <typename T>
RegistersMetadataEntry BuildMetadata(fidl::Allocator& allocator, uint32_t bind_id, uint32_t mmio_id,
                                     std::vector<MaskEntryBuilder<T>> masks) {
  fidl::VectorView<MaskEntry> built_masks;
  built_masks.set_data(allocator.make<MaskEntry[]>(masks.size()));
  built_masks.set_count(masks.size());
  for (uint32_t i = 0; i < masks.size(); i++) {
    built_masks[i] = MaskEntry::Builder(allocator.make<MaskEntry::Frame>())
                         .set_mask(allocator.make<Mask>(BuildMask<T>(allocator, masks[i].mask)))
                         .set_mmio_offset(allocator.make<uint64_t>(masks[i].mmio_offset))
                         .set_count(allocator.make<uint32_t>(masks[i].reg_count))
                         .set_overlap_check_on(allocator.make<bool>(masks[i].overlap_check_on))
                         .build();
  }

  return RegistersMetadataEntry::Builder(allocator.make<RegistersMetadataEntry::Frame>())
      .set_bind_id(allocator.make<uint32_t>(bind_id))
      .set_mmio_id(allocator.make<uint32_t>(mmio_id))
      .set_masks(allocator.make<fidl::VectorView<MaskEntry>>(std::move(built_masks)))
      .build();
}

using ::llcpp::fuchsia::hardware::registers::MmioMetadataEntry;
MmioMetadataEntry BuildMetadata(fidl::Allocator& allocator, uint32_t id) {
  return MmioMetadataEntry::Builder(allocator.make<MmioMetadataEntry::Frame>())
      .set_id(allocator.make<uint32_t>(id))
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
