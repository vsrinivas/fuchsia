// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/format.h>
#include <fvm/fvm-check.h>
#include <fvm/fvm.h>
#include <gpt/guid.h>

namespace fvm {

Checker::Checker() = default;

Checker::Checker(fbl::unique_fd fd, uint32_t block_size, bool silent)
    : fd_(std::move(fd)), block_size_(block_size), logger_(silent) {}

Checker::~Checker() = default;

bool Checker::Validate() const {
  if (!ValidateOptions()) {
    return false;
  }

  FvmInfo info;
  if (!LoadFVM(&info)) {
    return false;
  }

  return CheckFVM(info);
}

bool Checker::ValidateOptions() const {
  if (!fd_) {
    logger_.Error("FVM checker missing a device\n");
    return false;
  }
  if (block_size_ == 0) {
    logger_.Error("Invalid block size\n");
    return false;
  }
  return true;
}

bool Checker::LoadFVM(FvmInfo* out) const {
  const off_t device_size = lseek(fd_.get(), 0, SEEK_END);
  if (device_size < 0) {
    logger_.Error("Unable to get file length\n");
    return false;
  }
  if (device_size % block_size_ != 0) {
    logger_.Error("File size is not divisible by block size\n");
    return false;
  }
  const size_t block_count = device_size / block_size_;

  std::unique_ptr<uint8_t[]> header(new uint8_t[fvm::kBlockSize]);
  if (pread(fd_.get(), header.get(), fvm::kBlockSize, 0) != static_cast<ssize_t>(fvm::kBlockSize)) {
    logger_.Error("Could not read header\n");
    return false;
  }
  const fvm::Header* superblock = reinterpret_cast<fvm::Header*>(header.get());
  if (superblock->slice_size % block_size_ != 0) {
    logger_.Error("Slice size not divisible by block size\n");
    return false;
  } else if (superblock->slice_size == 0) {
    logger_.Error("Slice size cannot be zero\n");
    return false;
  }

  // Validate sizes to prevent allocating overlarge buffers for the metadata. Check the table
  // sizes separately to prevent numeric overflow when combining them.
  if (superblock->GetAllocationTableAllocatedByteSize() > fvm::kMaxAllocationTableByteSize) {
    logger_.Error("Slice allocation table is too large.");
    return false;
  }
  if (superblock->GetPartitionTableByteSize() > fvm::kMaxPartitionTableByteSize) {
    logger_.Error("FVM header partition table is too large.");
    return false;
  }

  size_t metadata_allocated_bytes = superblock->GetMetadataAllocatedBytes();
  if (metadata_allocated_bytes > fvm::kMaxMetadataByteSize) {
    logger_.Error("FVM metadata size exceeds maximum limit.");
    return false;
  }

  // The metadata buffer holds both primary and secondary copies of the metadata.
  size_t metadata_buffer_size = metadata_allocated_bytes * 2;
  std::unique_ptr<uint8_t[]> metadata(new uint8_t[metadata_buffer_size]);
  if (pread(fd_.get(), metadata.get(), metadata_buffer_size, 0) !=
      static_cast<ssize_t>(metadata_buffer_size)) {
    logger_.Error("Could not read metadata\n");
    return false;
  }

  std::optional<fvm::SuperblockType> use_superblock = fvm::ValidateHeader(
      metadata.get(), metadata.get() + metadata_allocated_bytes, metadata_allocated_bytes);
  if (!use_superblock) {
    logger_.Error("Invalid FVM metadata\n");
    return false;
  }

  fvm::SuperblockType invalid_superblock = *use_superblock == fvm::SuperblockType::kPrimary
                                               ? fvm::SuperblockType::kSecondary
                                               : fvm::SuperblockType::kPrimary;

  const uint8_t* valid_metadata = metadata.get() + superblock->GetSuperblockOffset(*use_superblock);
  const uint8_t* invalid_metadata =
      metadata.get() + superblock->GetSuperblockOffset(invalid_superblock);

  FvmInfo info = {
      fbl::Array<uint8_t>(metadata.release(), superblock->GetMetadataAllocatedBytes() * 2),
      superblock->GetSuperblockOffset(*use_superblock),
      valid_metadata,
      invalid_metadata,
      block_size_,
      block_count,
      static_cast<size_t>(device_size),
      superblock->slice_size,
  };

  *out = std::move(info);
  return true;
}

bool Checker::LoadPartitions(const size_t slice_count, const fvm::SliceEntry* slice_table,
                             const fvm::VPartitionEntry* vpart_table,
                             fbl::Vector<Slice>* out_slices,
                             fbl::Array<Partition>* out_partitions) const {
  fbl::Vector<Slice> slices;
  fbl::Array<Partition> partitions(new Partition[fvm::kMaxVPartitions], fvm::kMaxVPartitions);

  bool valid = true;

  // Initialize all allocated partitions.
  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    const uint32_t slices = vpart_table[i].slices;
    if (slices != 0) {
      partitions[i].entry = &vpart_table[i];
    }
  }

  // Initialize all slices, ensure they are used for allocated partitions.
  for (size_t i = 1; i <= slice_count; i++) {
    if (slice_table[i].IsAllocated()) {
      const uint64_t vpart = slice_table[i].VPartition();
      if (vpart >= kMaxVPartitions) {
        logger_.Error("Invalid vslice entry; claims vpart which is out of range.\n");
        valid = false;
      } else if (!partitions[vpart].entry || partitions[vpart].entry->IsFree()) {
        logger_.Error("Invalid slice entry; claims that it is allocated to unallocated ");
        logger_.Error("partition %zu\n", vpart);
        valid = false;
      }

      Slice slice = {vpart, slice_table[i].VSlice(), i};

      slices.push_back(slice);
      partitions[vpart].slices.push_back(std::move(slice));
    }
  }

  // Validate that all allocated partitions are correct about the number of slices used.
  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    if (partitions[i].Allocated()) {
      const size_t claimed = partitions[i].entry->slices;
      const size_t actual = partitions[i].slices.size();
      if (claimed != actual) {
        logger_.Error("Disagreement about allocated slice count: ");
        logger_.Error("Partition %zu claims %zu slices, has %zu\n", i, claimed, actual);
        valid = false;
      }
    }
  }

  *out_slices = std::move(slices);
  *out_partitions = std::move(partitions);
  return valid;
}

void Checker::DumpSlices(const fbl::Vector<Slice>& slices) const {
  logger_.Log("[  Slice Info  ]\n");
  Slice* run_start = nullptr;
  size_t run_length = 0;

  // Prints whatever information we can from the current contiguous range of
  // virtual / physical slices, then reset the "run" information.
  //
  // A run is a contiguous set of virtual / physical slices, all allocated to the same
  // virtual partition. Noncontiguity in either the virtual or physical range
  // "breaks" the run, since these cases provide new information.
  auto start_run = [&run_start, &run_length](Slice* slice) {
    run_start = slice;
    run_length = 1;
  };
  auto end_run = [this, &run_start, &run_length]() {
    if (run_length == 1) {
      logger_.Log("Physical Slice %zu allocated\n", run_start->physical_slice);
      logger_.Log("  Allocated as virtual slice %zu\n", run_start->virtual_slice);
      logger_.Log("  Allocated to partition %zu\n", run_start->virtual_partition);
    } else if (run_length > 1) {
      logger_.Log("%zu Physical Slices [%" PRIu64 ", %" PRIu64 "] allocated\n", run_length,
                  run_start->physical_slice, run_start->physical_slice + run_length - 1);
      logger_.Log("  Allocated as virtual slices [%zu, %zu]\n", run_start->virtual_slice,
                  run_start->virtual_slice + run_length - 1);
      logger_.Log("  Allocated to partition %zu\n", run_start->virtual_partition);
    }
    run_start = nullptr;
    run_length = 0;
  };

  if (!slices.is_empty()) {
    start_run(&slices[0]);
  }
  for (size_t i = 1; i < slices.size(); i++) {
    const auto& slice = slices[i];
    const size_t expected_pslice = run_start->physical_slice + run_length;
    const size_t expected_vslice = run_start->virtual_slice + run_length;
    if (slice.physical_slice == expected_pslice && slice.virtual_slice == expected_vslice &&
        slice.virtual_partition == run_start->virtual_partition) {
      run_length++;
    } else {
      end_run();
      start_run(&slices[i]);
    }
  }
  end_run();
}

bool Checker::CheckFVM(const FvmInfo& info) const {
  auto superblock = reinterpret_cast<const fvm::Header*>(info.valid_metadata);
  auto invalid_superblock = reinterpret_cast<const fvm::Header*>(info.invalid_metadata);

  logger_.Log("[  FVM Info  ]\n");
  logger_.Log("Version: %" PRIu64 "\n", superblock->version);
  logger_.Log("Generation number: %" PRIu64 "\n", superblock->generation);
  logger_.Log("Generation number: %" PRIu64 " (invalid copy)\n", invalid_superblock->generation);
  logger_.Log("\n");

  const size_t slice_count = superblock->GetAllocationTableUsedEntryCount();
  logger_.Log("[  Size Info  ]\n");
  logger_.Log("%-15s %10zu\n", "Device Length:", info.device_size);
  logger_.Log("%-15s %10zu\n", "Block size:", info.block_size);
  logger_.Log("%-15s %10zu\n", "Slice size:", info.slice_size);
  logger_.Log("%-15s %10zu\n", "Slice count:", slice_count);
  logger_.Log("\n");

  const size_t metadata_size = superblock->GetMetadataAllocatedBytes();
  const size_t metadata_count = 2;
  const size_t metadata_end = metadata_size * metadata_count;
  logger_.Log("[  Metadata  ]\n");
  logger_.Log("%-25s 0x%016zx\n", "Valid metadata start:", info.valid_metadata_offset);
  logger_.Log("%-25s 0x%016x\n", "Metadata start:", 0);
  logger_.Log("%-25s   %16zu (for each copy)\n", "Metadata size:", metadata_size);
  logger_.Log("%-25s   %16zu\n", "Metadata count:", metadata_count);
  logger_.Log("%-25s 0x%016zx\n", "Metadata end:", metadata_end);
  logger_.Log("\n");

  logger_.Log("[  All Subsequent Offsets Relative to Valid Metadata Start  ]\n");
  logger_.Log("\n");

  const size_t vpart_table_start = superblock->GetPartitionTableOffset();
  const size_t vpart_entry_size = sizeof(fvm::VPartitionEntry);
  const size_t vpart_table_size = superblock->GetPartitionTableByteSize();
  const size_t vpart_table_end = vpart_table_start + vpart_table_size;
  logger_.Log("[  Virtual Partition Table  ]\n");
  logger_.Log("%-25s 0x%016zx\n", "VPartition Entry Start:", vpart_table_start);
  logger_.Log("%-25s   %16zu\n", "VPartition entry size:", vpart_entry_size);
  logger_.Log("%-25s   %16zu\n", "VPartition table size:", vpart_table_size);
  logger_.Log("%-25s 0x%016zx\n", "VPartition table end:", vpart_table_end);
  logger_.Log("\n");

  const size_t slice_table_start = superblock->GetAllocationTableOffset();
  const size_t slice_entry_size = sizeof(fvm::SliceEntry);
  const size_t slice_table_size = superblock->GetAllocationTableUsedByteSize();
  const size_t slice_table_end = slice_table_start + slice_table_size;
  logger_.Log("[  Slice Allocation Table  ]\n");
  logger_.Log("%-25s 0x%016zx\n", "Slice table start:", slice_table_start);
  logger_.Log("%-25s   %16zu\n", "Slice entry size:", slice_entry_size);
  logger_.Log("%-25s   %16zu\n", "Slice table size:", slice_table_size);
  logger_.Log("%-25s 0x%016zx\n", "Slice table end:", slice_table_end);
  logger_.Log("\n");

  const fvm::SliceEntry* slice_table =
      reinterpret_cast<const fvm::SliceEntry*>(info.valid_metadata + slice_table_start);
  const fvm::VPartitionEntry* vpart_table =
      reinterpret_cast<const fvm::VPartitionEntry*>(info.valid_metadata + vpart_table_start);

  fbl::Vector<Slice> slices;
  fbl::Array<Partition> partitions;
  bool valid = true;
  if (!LoadPartitions(slice_count, slice_table, vpart_table, &slices, &partitions)) {
    valid = false;
    logger_.Log("Partitions invalid; displaying info anyway...\n");
  }

  logger_.Log("[  Partition Info  ]\n");
  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    const uint32_t slices = vpart_table[i].slices;
    if (slices != 0) {
      logger_.Log("Partition %zu allocated\n", i);
      logger_.Log("  Has %u slices allocated\n", slices);
      logger_.Log("  Type: %s\n", gpt::KnownGuid::TypeDescription(vpart_table[i].type).c_str());
      logger_.Log("  Name: %s\n", vpart_table[i].name().c_str());
    }
  }
  logger_.Log("\n");

  DumpSlices(slices);
  return valid;
}

}  // namespace fvm
