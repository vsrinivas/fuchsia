// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
#define SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_

#include <fidl/fuchsia.hardware.registers/cpp/wire.h>

#include <vector>

namespace registers {

using fuchsia_hardware_registers::wire::Mask;
template <typename T>
Mask BuildMask(fidl::AnyArena& allocator, T mask) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return Mask::WithR8(mask);
  }
  if constexpr (std::is_same_v<T, uint16_t>) {
    return Mask::WithR16(mask);
  }
  if constexpr (std::is_same_v<T, uint32_t>) {
    return Mask::WithR32(mask);
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
using fuchsia_hardware_registers::wire::MaskEntry;
using fuchsia_hardware_registers::wire::RegistersMetadataEntry;
template <typename T>
RegistersMetadataEntry BuildMetadata(fidl::AnyArena& allocator, uint32_t bind_id, uint32_t mmio_id,
                                     std::vector<MaskEntryBuilder<T>> masks) {
  fidl::VectorView<MaskEntry> built_masks(allocator, masks.size());
  for (uint32_t i = 0; i < masks.size(); i++) {
    built_masks[i] = MaskEntry::Builder(allocator)
                         .mask(BuildMask<T>(allocator, masks[i].mask))
                         .mmio_offset(masks[i].mmio_offset)
                         .count(masks[i].reg_count)
                         .overlap_check_on(masks[i].overlap_check_on)
                         .Build();
  }

  return RegistersMetadataEntry::Builder(allocator)
      .bind_id(bind_id)
      .mmio_id(mmio_id)
      .masks(built_masks)
      .Build();
}

using fuchsia_hardware_registers::wire::MmioMetadataEntry;
MmioMetadataEntry BuildMetadata(fidl::AnyArena& allocator, uint32_t id) {
  return MmioMetadataEntry::Builder(allocator).id(id).Build();
}

using fuchsia_hardware_registers::wire::Metadata;
Metadata BuildMetadata(fidl::AnyArena& allocator, fidl::VectorView<MmioMetadataEntry> mmio,
                       fidl::VectorView<RegistersMetadataEntry> registers) {
  return Metadata::Builder(allocator).mmio(mmio).registers(registers).Build();
}

}  // namespace registers

#endif  // SRC_DEVICES_LIB_METADATA_LLCPP_REGISTERS_H_
