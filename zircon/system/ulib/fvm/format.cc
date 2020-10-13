// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <sstream>
#include <string>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/volume/c/fidl.h>
#endif

#include <zircon/assert.h>

#include <fvm/format.h>

namespace fvm {
namespace {

// Used to check whether a given VPartitionEntry is flagged as an inactive partition.
// This flags are a mirror of those exposed in the fidl interface. Since this code is used in host
// too, we can't rely on them directly, but enforce compile time checks that the values match.
constexpr uint32_t kVPartitionEntryFlagMask = 0x00000001;
constexpr uint32_t kVPartitionEntryFlagInactive = 0x00000001;

#ifdef __Fuchsia__
// Enforce target and host flags to match.
static_assert(kVPartitionEntryFlagInactive ==
                  fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE,
              "Inactive Flag must match FIDL definition.");
#endif

// Slice Entry mask for retrieving the assigned partition.
constexpr uint64_t kVPartitionEntryMax = (1ull << kSliceEntryVPartitionBits) - 1;
constexpr uint64_t kVPartitionEntryMask = kVPartitionEntryMax;

static_assert(kMaxVPartitions <= kVPartitionEntryMax,
              "VPartition addres space needs to fit within Slice Entry VPartitionBits.");

// Slice Entry mask for retrieving the assigned vslice.
constexpr uint64_t kSliceEntryVSliceMax = (1ull << kSliceEntryVSliceBits) - 1;
constexpr uint64_t kSliceEntryVSliceMask = kSliceEntryVSliceMax << kSliceEntryVPartitionBits;

static_assert(kSliceEntryVSliceMax >= fvm::kMaxVSlices,
              "SliceEntry must be able to address the range [0. kMaxVSlice)");

// Remaining bits.
constexpr uint64_t kSliceEntryReservedBits = 16;

static_assert(kSliceEntryVPartitionBits + kSliceEntryVSliceBits + kSliceEntryReservedBits == 64,
              "Exceeding SliceEntry payload size.");

// Returns how large one copy of the metadata is for the given table settings.
constexpr size_t MetadataSizeForUsableEntries(size_t usable_partitions, size_t usable_slices) {
  return kBlockSize +                                                    // Superblock
         PartitionTableByteSizeForUsablePartitions(usable_partitions) +  // Partition table.
         AllocTableLengthForUsableSliceCount(usable_slices);
}

constexpr size_t DataStartForUsableEntries(size_t usable_partitions, size_t usable_slices) {
  // The data starts after the two copies of the metadata.
  return MetadataSizeForUsableEntries(usable_partitions, usable_slices) * 2;
}

}  // namespace

Header Header::FromDiskSize(size_t usable_partitions, size_t disk_size, size_t slice_size) {
  return FromGrowableDiskSize(usable_partitions, disk_size, disk_size, slice_size);
}

Header Header::FromGrowableDiskSize(size_t usable_partitions, size_t initial_disk_size,
                                    size_t max_disk_size, size_t slice_size) {
  // The relationship between the minimum number of slices required and the disk size is nonlinear
  // because the metadata takes away from the usable disk space covered by the slices and the
  // allocation table size is always block-aligned.
  //
  // Here we ignore this and just compute the metadata size based on the number of slices required
  // to cover the entire device, even though we don't need a slice to cover the copies of the
  // metadata.
  //
  // This function always rounds down because we can't have partial slices. If the non-metadata
  // space isn't a multiple of the slice size, there will be some unusable space at the end.
  size_t max_usable_slices = max_disk_size / slice_size;

  // Compute the initial slice count. Unlike when calculating the max usable slices, we can't ignore
  // the metadata size since the caller expects the metadata and the used slices to fit in the
  // requested disk size.
  size_t slice_data_start = DataStartForUsableEntries(usable_partitions, max_usable_slices);
  size_t initial_slices = 0;
  if (initial_disk_size > slice_data_start)
    initial_slices = (initial_disk_size - slice_data_start) / slice_size;

  return FromGrowableSliceCount(usable_partitions, initial_slices, max_usable_slices, slice_size);
}

Header Header::FromSliceCount(size_t usable_partitions, size_t usable_slices, size_t slice_size) {
  return FromGrowableSliceCount(usable_partitions, usable_slices, usable_slices, slice_size);
}

Header Header::FromGrowableSliceCount(size_t usable_partitions, size_t initial_usable_slices,
                                      size_t max_usable_slices, size_t slice_size) {
  // Slice size must be a multiple of the block size.
  ZX_ASSERT(slice_size % kBlockSize == 0);

  // TODO(fxb/40192): Allow the partition table to vary.
  ZX_ASSERT(usable_partitions == kMaxUsablePartitions);
  Header result{
      .magic = kMagic,
      .version = kVersion,
      .pslice_count = 0,  // Will be set properly below.
      .slice_size = slice_size,
      .fvm_partition_size = kBlockSize,  // Will be set properly below.
      .vpartition_table_size = PartitionTableByteSizeForUsablePartitions(usable_partitions),
      .allocation_table_size = AllocTableLengthForUsableSliceCount(max_usable_slices),
      .generation = 0,
  };

  // Set the pslice_count and fvm_partition_size now that we know the metadata size.
  result.SetSliceCount(initial_usable_slices);

  return result;
}

std::string Header::ToString() const {
  std::stringstream ss;
  ss << "FVM Header" << std::endl;
  ss << "  magic: " << std::to_string(magic) << std::endl;
  ss << "  version: " << std::to_string(version) << std::endl;
  ss << "  pslice_count: " << std::to_string(pslice_count) << std::endl;
  ss << "  slice_size: " << std::to_string(slice_size) << std::endl;
  ss << "  fvm_partition_size: " << std::to_string(fvm_partition_size) << std::endl;
  ss << "  vpartition_table_size: " << std::to_string(vpartition_table_size) << std::endl;
  ss << "  allocation_table_size: " << std::to_string(allocation_table_size) << std::endl;
  ss << "  generation: " << std::to_string(generation) << std::endl;
  return ss.str();
}

VPartitionEntry VPartitionEntry::Create(const uint8_t* type, const uint8_t* guid, uint32_t slices,
                                        Name name, uint32_t flags) {
  VPartitionEntry entry = VPartitionEntry::Create();
  entry.slices = slices;
  // Filter out unallowed flags.
  entry.flags = ParseFlags(flags);
  memcpy(&entry.type, type, kGuidSize);
  memcpy(&entry.guid, guid, kGuidSize);
  const size_t name_len = std::min<size_t>(kMaxVPartitionNameLength, name.name.size());
  memcpy(entry.unsafe_name, name.name.data(), name_len);
  memset(&entry.unsafe_name[name_len], 0, kMaxVPartitionNameLength - name_len);
  return entry;
}

uint32_t VPartitionEntry::ParseFlags(uint32_t raw_flags) {
  return raw_flags & kVPartitionEntryFlagMask;
}

bool VPartitionEntry::IsActive() const { return (flags & kVPartitionEntryFlagInactive) == 0; }

bool VPartitionEntry::IsInactive() const { return !IsActive(); }

bool VPartitionEntry::IsAllocated() const { return slices != 0; }

bool VPartitionEntry::IsFree() const { return !IsAllocated(); }

void VPartitionEntry::Release() {
  memset(this, 0, sizeof(VPartitionEntry));
  ZX_ASSERT_MSG(IsFree(), "VPartitionEntry must be free after calling VPartitionEntry::Release()");
}

void VPartitionEntry::SetActive(bool is_active) {
  if (is_active) {
    flags &= (~kVPartitionEntryFlagInactive);
  } else {
    flags |= kVPartitionEntryFlagInactive;
  }
}

SliceEntry SliceEntry::Create(uint64_t vpartition, uint64_t vslice) {
  SliceEntry entry;
  entry.Set(vpartition, vslice);
  return entry;
}

void SliceEntry::Set(uint64_t vpartition, uint64_t vslice) {
  ZX_ASSERT(vpartition < kVPartitionEntryMax);
  ZX_ASSERT(vslice < kSliceEntryVSliceMax);
  data = 0ull | (vpartition & kVPartitionEntryMax) |
         ((vslice & kSliceEntryVSliceMax) << (kSliceEntryVPartitionBits));
}

void SliceEntry::Release() { data = 0; }

bool SliceEntry::IsAllocated() const { return VPartition() != 0; }

bool SliceEntry::IsFree() const { return !IsAllocated(); }

uint64_t SliceEntry::VSlice() const {
  uint64_t vslice = (data & kSliceEntryVSliceMask) >> kSliceEntryVPartitionBits;
  ZX_ASSERT_MSG(vslice < (1ul << kSliceEntryVSliceBits), "Slice assigned to vslice out of range.");
  return vslice;
}

uint64_t SliceEntry::VPartition() const {
  uint64_t vpartition = (data & kVPartitionEntryMask);
  ZX_ASSERT_MSG(vpartition < kMaxVPartitions, "Slice assigned to Partition out of range.");
  return vpartition;
}

}  // namespace fvm
