// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <sstream>
#include <string>

#ifdef __Fuchsia__
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#endif

#include <zircon/assert.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/fvm/format.h"

namespace fvm {
namespace {

static_assert(kGuidSize == uuid::kUuidSize, "Guid size doesn't match");

// Used to check whether a given VPartitionEntry is flagged as an inactive partition.
// This flags are a mirror of those exposed in the fidl interface. Since this code is used in host
// too, we can't rely on them directly, but enforce compile time checks that the values match.
constexpr uint32_t kVPartitionEntryFlagMask = 0x00000001;
constexpr uint32_t kVPartitionEntryFlagInactive = 0x00000001;

#ifdef __Fuchsia__
// Enforce target and host flags to match.
static_assert(kVPartitionEntryFlagInactive ==
                  fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive,
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
  return kBlockSize +                                                        // Superblock
         PartitionTableByteSizeForUsablePartitionCount(usable_partitions) +  // Partition table.
         AllocTableByteSizeForUsableSliceCount(usable_slices);
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
      .major_version = kCurrentMajorVersion,
      .pslice_count = 0,  // Will be set properly below.
      .slice_size = slice_size,
      .fvm_partition_size = kBlockSize,  // Will be set properly below.
      .vpartition_table_size = PartitionTableByteSizeForUsablePartitionCount(usable_partitions),
      .allocation_table_size = AllocTableByteSizeForUsableSliceCount(max_usable_slices),
      .generation = 0,
      .oldest_minor_version = kCurrentMinorVersion,
  };

  // Set the pslice_count and fvm_partition_size now that we know the metadata size.
  result.SetSliceCount(initial_usable_slices);

  return result;
}

bool Header::IsValid(uint64_t disk_size, uint64_t disk_block_size, std::string& out_err) const {
  // Magic.
  if (magic != kMagic) {
    out_err = "Bad magic value for FVM header.\n" + ToString();
    return false;
  }

  // Check version.
  if (major_version > kCurrentMajorVersion) {
    out_err =
        "Header major version does not match fvm driver (=" + std::to_string(kCurrentMajorVersion) +
        ")\n" + ToString();
    return false;
  }

  // Slice count. This is important to check before using it below to prevent integer overflows.
  if (pslice_count > kMaxVSlices) {
    out_err =
        "Slice count is greater than the max (" + std::to_string(kMaxVSlices) + ")\n" + ToString();
    return false;
  }

  // Check the slice size.
  //
  // It's not currently clear whether we currently require fvm::kBlockSize to be a multiple of the
  // disk_block_size. If that requirement is solidifed in the future, that should be checked here.
  if (slice_size > kMaxSliceSize) {
    out_err = "Slice size would overflow 64 bits\n" + ToString();
    return false;
  }
  if (slice_size % disk_block_size != 0) {
    out_err = "Slice size is not a multiple of the underlying disk's block size (" +
              std::to_string(disk_block_size) + ")\n" + ToString();
    return false;
  }

  // Check partition and allocation table validity. Here we also perform additional validation on
  // the allocation table that uses the pslice_count which is not checked by HasValidTableSizes().
  if (!HasValidTableSizes(out_err))
    return false;
  size_t required_alloc_table_len = AllocTableByteSizeForUsableSliceCount(pslice_count);
  if (allocation_table_size < required_alloc_table_len) {
    out_err = "Expected allocation table to be at least " +
              std::to_string(required_alloc_table_len) + "\n" + ToString();
    return false;
  }

  // The partition must fit in the disk.
  if (fvm_partition_size > disk_size) {
    out_err = "Block device (" + std::to_string(disk_size) +
              " bytes) too small for fvm_partition_size\n" + ToString();
    return false;
  }

  // The header and addressable slices must fit in the partition.
  //
  // The required_data_bytes won't overflow because we already checked that pslice_count and
  // slice_size are in range, that that range is specified to avoid overflow.
  size_t required_data_bytes = GetAllocationTableUsedEntryCount() * slice_size;
  size_t max_addressable_bytes = std::numeric_limits<size_t>::max() - GetDataStartOffset();
  if (required_data_bytes > max_addressable_bytes) {
    out_err = "Slice data (" + std::to_string(required_data_bytes) + " bytes) + metadata (" +
              std::to_string(GetDataStartOffset()) + " bytes) exceeds max\n" + ToString();
    return false;
  }
  size_t required_partition_size = GetDataStartOffset() + required_data_bytes;
  if (required_partition_size > fvm_partition_size) {
    out_err = "Slices + metadata requires " + std::to_string(required_partition_size) +
              " bytes which don't fit in fvm_partition_size\n" + ToString();
    return false;
  }

  return true;
}

bool Header::HasValidTableSizes(std::string& out_err) const {
  // TODO(fxb/40192) Allow the partition table to be different lengths (aligned to blocks):
  //   size_t kMinPartitionTableSize = kBlockSize;
  //   if (sb.vpartition_table_size < kMinPartitionTableSize ||
  //       sb.vpartition_table_size > kMaxPartitionTableByteSize ||
  //       sb.vpartition_table_size % sizeof(VPartitionEntry) != 0) {
  //     out_err = ...
  //     return false;
  //
  // Currently the partition table must be a fixed size:
  size_t kPartitionTableLength = fvm::kMaxPartitionTableByteSize;
  if (vpartition_table_size != kPartitionTableLength) {
    out_err = "Bad vpartition table size.\n" + ToString();
    return false;
  }

  // Validate the allocation table size.
  if (allocation_table_size > kMaxAllocationTableByteSize ||
      allocation_table_size % kBlockSize != 0) {
    out_err = "Bad allocation table size " + std::to_string(allocation_table_size) +
              ", expected nonzero multiple of " + std::to_string(kBlockSize) + "\n" + ToString();
    return false;
  }

  return true;
}

std::string Header::ToString() const {
  std::stringstream ss;
  ss << "FVM Header" << std::endl;
  ss << "  magic: " << magic << std::endl;
  ss << "  major_version: " << major_version << std::endl;
  ss << "  pslice_count: " << pslice_count << std::endl;
  ss << "  slice_size: " << slice_size << std::endl;
  ss << "  fvm_partition_size: " << fvm_partition_size << std::endl;
  ss << "  vpartition_table_size: " << vpartition_table_size << std::endl;
  ss << "  allocation_table_size: " << allocation_table_size << std::endl;
  ss << "  generation: " << generation << std::endl;
  ss << "  oldest_minor_version: " << oldest_minor_version << std::endl;
  return ss.str();
}

// This is compact so it can fit in a single syslog line.
std::ostream& operator<<(std::ostream& out, const Header& header) {
  return out << "v" << header.major_version << "." << header.oldest_minor_version
             << " slices:" << header.pslice_count << " slice_size:" << header.slice_size
             << " total_part:" << header.fvm_partition_size
             << " ptab:" << header.vpartition_table_size << " atab:" << header.allocation_table_size
             << " gen:" << header.generation;
}

VPartitionEntry::VPartitionEntry(const uint8_t in_type[kGuidSize], const uint8_t in_guid[kGuidSize],
                                 uint32_t in_slices, std::string in_name, uint32_t in_flags)
    : slices(in_slices), flags(MaskInvalidFlags(in_flags)) {
  memcpy(&type, in_type, kGuidSize);
  memcpy(&guid, in_guid, kGuidSize);

  // The input name should not have any embedded nulls.
  ZX_DEBUG_ASSERT(in_name.find('\0') == std::string::npos);
  memcpy(unsafe_name, in_name.data(), std::min(kMaxVPartitionNameLength, in_name.size()));
}

VPartitionEntry VPartitionEntry::CreateReservedPartition() {
  constexpr const char* kName = "internal";
  static_assert(__builtin_strlen(kName) <= kMaxVPartitionNameLength);
  return VPartitionEntry(kReservedPartitionTypeGuid.cbegin(), kReservedPartitionTypeGuid.cbegin(),
                         0, kName, 0);
}

uint32_t VPartitionEntry::MaskInvalidFlags(uint32_t raw_flags) {
  return raw_flags & kVPartitionEntryFlagMask;
}

bool VPartitionEntry::IsActive() const { return (flags & kVPartitionEntryFlagInactive) == 0; }

bool VPartitionEntry::IsInactive() const { return !IsActive(); }

bool VPartitionEntry::IsAllocated() const { return slices != 0; }

bool VPartitionEntry::IsFree() const { return !IsAllocated(); }

bool VPartitionEntry::IsInternalReservationPartition() const {
  return memcmp(type, kReservedPartitionTypeGuid.cbegin(), sizeof(type)) == 0;
}

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

std::ostream& operator<<(std::ostream& out, const VPartitionEntry& entry) {
  // This is deliberately compact so it can be logged to the system log which has a max per-line
  // length.
  out << "\"" << entry.name() << "\" slices:" << entry.slices;
  out << " flags:" << entry.flags << " (act=" << entry.IsActive() << ")";
  out << " type:" << uuid::Uuid(entry.type);
  out << " guid:" << uuid::Uuid(entry.guid);
  return out;
}

SliceEntry::SliceEntry(uint64_t vpartition, uint64_t vslice) { Set(vpartition, vslice); }

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

std::ostream& operator<<(std::ostream& out, const SliceEntry& entry) {
  if (entry.IsFree())
    return out << "SliceEntry(<free>)";
  return out << "SliceEntry(vpartition=" << entry.VPartition() << ", vslice=" << entry.VSlice()
             << ")";
}

}  // namespace fvm
