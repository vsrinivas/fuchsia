// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
#define SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_

#include <fuchsia/hardware/registers/llcpp/fidl.h>

namespace registers {

using ::llcpp::fuchsia::hardware::registers::Mask;
template <typename T>
Mask BuildMask(fidl::AnyAllocator& allocator, T mask) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return Mask::WithR8(allocator, mask);
  }
  if constexpr (std::is_same_v<T, uint16_t>) {
    return Mask::WithR16(allocator, mask);
  }
  if constexpr (std::is_same_v<T, uint32_t>) {
    return Mask::WithR32(allocator, mask);
  }
  if constexpr (std::is_same_v<T, uint64_t>) {
    return Mask::WithR64(allocator, mask);
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
RegistersMetadataEntry BuildMetadata(fidl::AnyAllocator& allocator, uint32_t bind_id,
                                     uint32_t mmio_id, std::vector<MaskEntryBuilder<T>> masks) {
  fidl::VectorView<MaskEntry> built_masks(allocator, masks.size());
  for (uint32_t i = 0; i < masks.size(); i++) {
    built_masks[i].Allocate(allocator);
    built_masks[i].set_mask(allocator, BuildMask<T>(allocator, masks[i].mask));
    built_masks[i].set_mmio_offset(allocator, masks[i].mmio_offset);
    built_masks[i].set_count(allocator, masks[i].reg_count);
    built_masks[i].set_overlap_check_on(allocator, masks[i].overlap_check_on);
  }

  RegistersMetadataEntry entry(allocator);
  entry.set_bind_id(allocator, bind_id);
  entry.set_mmio_id(allocator, mmio_id);
  entry.set_masks(allocator, std::move(built_masks));
  return entry;
}

using ::llcpp::fuchsia::hardware::registers::MmioMetadataEntry;
MmioMetadataEntry BuildMetadata(fidl::AnyAllocator& allocator, uint32_t id) {
  MmioMetadataEntry entry(allocator);
  entry.set_id(allocator, id);
  return entry;
}

using ::llcpp::fuchsia::hardware::registers::Metadata;
Metadata BuildMetadata(fidl::AnyAllocator& allocator, fidl::VectorView<MmioMetadataEntry> mmio,
                       fidl::VectorView<RegistersMetadataEntry> registers) {
  Metadata metadata(allocator);
  metadata.set_mmio(allocator, std::move(mmio));
  metadata.set_registers(allocator, std::move(registers));
  return metadata;
}

}  // namespace registers

#endif  // SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
