// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <algorithm>
#include <sstream>
#include <string>

#include <fvm/snapshot-metadata-format.h>
#include <range/range.h>

namespace fvm {

SnapshotMetadataHeader::SnapshotMetadataHeader()
    : SnapshotMetadataHeader(kSnapshotMetadataHeaderMinPartitions,
                             kSnapshotMetadataHeaderMinExtentTypes) {}

SnapshotMetadataHeader::SnapshotMetadataHeader(uint32_t partition_state_table_entries,
                                               uint32_t extent_type_table_entries) {
  partition_state_table_entry_count =
      std::clamp(partition_state_table_entries, kSnapshotMetadataHeaderMinPartitions,
                 kSnapshotMetadataHeaderMaxPartitions);
  extent_type_table_entry_count =
      std::clamp(extent_type_table_entries, kSnapshotMetadataHeaderMinExtentTypes,
                 kSnapshotMetadataHeaderMaxExtentTypes);

  // Partition state table can go at a fixed offset after the header.
  partition_state_table_offset = kSnapshotMetadataHeaderMaxSize;

  // Extent type table can go immediately after.
  extent_type_table_offset = PartitionStateTableOffset() + PartitionStateTableSizeBytes();

  ZX_ASSERT(PartitionStateTableOffset() + PartitionStateTableSizeBytes() <=
            kSnapshotMetadataSecondHeaderOffset);
  ZX_ASSERT(ExtentTypeTableOffset() + ExtentTypeTableSizeBytes() <=
            kSnapshotMetadataSecondHeaderOffset);
}

bool SnapshotMetadataHeader::IsValid(std::string& out_error) const {
  if (magic != kSnapshotMetadataMagic) {
    out_error = "fvm snapshot magic invalid";
    return false;
  }

  if (format_version > kSnapshotMetadataCurrentFormatVersion) {
    out_error = "fvm snapshot metadata version does not match fvm driver (=" +
                std::to_string(kSnapshotMetadataCurrentFormatVersion) + ")\n" + ToString();
    return false;
  }

  if (partition_state_table_entry_count < kSnapshotMetadataHeaderMinPartitions ||
      partition_state_table_entry_count > kSnapshotMetadataHeaderMaxPartitions) {
    out_error = "invalid partition state table sz (" +
                std::to_string(partition_state_table_entry_count) + ")\n" + ToString();
    return false;
  }

  if (extent_type_table_entry_count < kSnapshotMetadataHeaderMinExtentTypes ||
      extent_type_table_entry_count > kSnapshotMetadataHeaderMaxExtentTypes) {
    out_error = "invalid extent type table sz (" + std::to_string(extent_type_table_entry_count) +
                ")\n" + ToString();
    return false;
  }

  range::Range<size_t> header_range(
      HeaderOffset(SnapshotMetadataCopy::kPrimary),
      HeaderOffset(SnapshotMetadataCopy::kPrimary) + kSnapshotMetadataHeaderMaxSize);
  range::Range<size_t> second_header_range(
      HeaderOffset(SnapshotMetadataCopy::kSecondary),
      HeaderOffset(SnapshotMetadataCopy::kSecondary) + kSnapshotMetadataHeaderMaxSize);
  range::Range<size_t> part_state_table_range(
      PartitionStateTableOffset(), PartitionStateTableOffset() + PartitionStateTableSizeBytes());
  range::Range<size_t> extent_type_table_range(
      ExtentTypeTableOffset(), ExtentTypeTableOffset() + ExtentTypeTableSizeBytes());
  const std::array<const range::Range<size_t>*, 4> ranges{
      &header_range,
      &second_header_range,
      &part_state_table_range,
      &extent_type_table_range,
  };
  for (const auto* range1 : ranges) {
    for (const auto* range2 : ranges) {
      if (range1 == range2)
        continue;
      if (range::Overlap(*range1, *range2)) {
        out_error = "Metadata regions overlap\n" + ToString();
        return false;
      }
    }
  }
  return true;
}

size_t SnapshotMetadataHeader::PartitionStateTableSizeBytes() const {
  return partition_state_table_entry_count * sizeof(PartitionSnapshotState);
}

size_t SnapshotMetadataHeader::ExtentTypeTableSizeBytes() const {
  return extent_type_table_entry_count * sizeof(SnapshotExtentType);
}

size_t SnapshotMetadataHeader::AllocatedMetadataBytes() const {
  return HeaderOffset(SnapshotMetadataCopy::kSecondary);
}

uint64_t SnapshotMetadataHeader::HeaderOffset(SnapshotMetadataCopy copy) {
  switch (copy) {
    case SnapshotMetadataCopy::kPrimary:
      return 0;
    case SnapshotMetadataCopy::kSecondary:
      return kSnapshotMetadataSecondHeaderOffset;
  }
}

std::string SnapshotMetadataHeader::ToString() const {
  std::stringstream ss;
  ss << "FVM Snapshot Metadata Header" << std::endl;
  ss << "  magic: " << magic << std::endl;
  ss << "  partition_state_table_offset: " << partition_state_table_offset << std::endl;
  ss << "  partition_state_table_entry_count: " << partition_state_table_entry_count << std::endl;
  ss << "  extent_type_table_offset: " << extent_type_table_offset << std::endl;
  ss << "  extent_type_table_entry_count: " << extent_type_table_entry_count << std::endl;
  return ss.str();
}

void PartitionSnapshotState::Release() { data = 0; }

SnapshotExtentType::SnapshotExtentType(uint64_t vpart, uint64_t vslice_offset,
                                       uint64_t extent_length_slices, ExtentType type)
    : vslice_offset(vslice_offset),
      extent_length_slices(extent_length_slices),
      vpartition_index(vpart),
      type(type) {}

bool SnapshotExtentType::IsFree() const { return vpartition_index == 0; }

void SnapshotExtentType::Release() { vpartition_index = 0; }

}  // namespace fvm
