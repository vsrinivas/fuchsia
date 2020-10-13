// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <memory>

#include <fvm-host/format.h>
#include <fvm-host/fvm-info.h>
#include <fvm/format.h>
#include <fvm/fvm.h>
#include <fvm/metadata.h>

zx_status_t FvmInfo::Reset(size_t disk_size, size_t slice_size) {
  if (slice_size == 0) {
    fprintf(stderr, "Invalid slice size\n");
    return ZX_ERR_INVALID_ARGS;
  }

  fvm::Header header = fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size, slice_size);
  auto metadata_or = fvm::Metadata::Synthesize(header, nullptr, 0, nullptr, 0);
  if (metadata_or.is_error()) {
    fprintf(stderr, "Failed to create metadata: %d\n", metadata_or.status_value());
    return metadata_or.status_value();
  }
  metadata_ = std::move(metadata_or.value());

  valid_ = true;
  dirty_ = true;

  xprintf("fvm_init: Success\n");
  xprintf("fvm_init: Slice Count: %" PRIu64 ", size: %" PRIu64 "\n", sb->pslice_count,
          sb->slice_size);
  xprintf("fvm_init: Vpart offset: %zu, length: %zu\n", sb->GetPartitionTableOffset(),
          sb->GetPartitionTableByteSize());
  xprintf("fvm_init: Atable offset: %zu, length: %zu\n", sb->GetAllocationTableOffset(),
          sb->GetAllocationTableAllocatedByteSize());
  xprintf("fvm_init: Backup meta starts at: %zu\n", fvm::BackupStart(disk_size, slice_size));
  xprintf("fvm_init: Slices start at %zu, there are %zu of them\n",
          fvm::SlicesStart(disk_size, slice_size), sb->GetAllocationTableAllocatedEntryCount());

  return ZX_OK;
}

zx_status_t FvmInfo::Load(fvm::host::FileWrapper* file, uint64_t disk_offset, uint64_t disk_size) {
  uint64_t start_position = file->Tell();

  if (disk_size == 0) {
    return ZX_OK;
  }

  fvm::Header header;

  // If Container already exists, read metadata from disk.
  // Read superblock first so we can determine if container has a different slice size.
  file->Seek(disk_offset, SEEK_SET);
  ssize_t result = file->Read(&header, sizeof(header));
  file->Seek(start_position, SEEK_SET);
  if (result != static_cast<ssize_t>(sizeof(fvm::Header))) {
    fprintf(stderr, "Superblock read failed: expected %ld, actual %ld\n", sizeof(fvm::Header),
            result);
    return ZX_ERR_IO;
  }

  // Read remainder of metadata.
  size_t metadata_size = fvm::MetadataBuffer::BytesNeeded(header);
  std::unique_ptr<uint8_t[]> metadata_raw(new uint8_t[metadata_size]);
  file->Seek(disk_offset, SEEK_SET);
  result = file->Read(metadata_raw.get(), metadata_size);
  file->Seek(start_position, SEEK_SET);
  if (result != static_cast<ssize_t>(metadata_size)) {
    fprintf(stderr, "Superblock read failed: expected %ld, actual %ld\n", metadata_size, result);
    return ZX_ERR_IO;
  }

  auto metadata_or = fvm::Metadata::Create(
      std::make_unique<fvm::HeapMetadataBuffer>(std::move(metadata_raw), metadata_size));
  if (metadata_or.is_error()) {
    return metadata_or.status_value();
  }
  metadata_ = std::move(metadata_or.value());

  if (DiskSize() != disk_size) {
    fprintf(stderr, "Disk size does not match expected");
    return ZX_ERR_BAD_STATE;
  }

  valid_ = true;
  if (!Validate()) {
    if (metadata_.active_header() != fvm::SuperblockType::kPrimary) {
      fprintf(stderr, "Can only update FVM with valid primary as first copy\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
    valid_ = false;
  }
  return ZX_OK;
}

bool FvmInfo::Validate() const {
  if (!metadata_.CheckValidity()) {
    return false;
  }
  return (metadata_.active_header() == fvm::SuperblockType::kPrimary);
}

zx_status_t FvmInfo::Write(fvm::host::FileWrapper* file, size_t disk_offset, size_t disk_size) {
  if (disk_size != SuperBlock().fvm_partition_size) {
    // If the disk size has changed, update and attempt to grow metadata.
    const fvm::Header header =
        fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size, SliceSize());
    if (zx_status_t status = Grow(header); status != ZX_OK) {
      return status;
    }
  }

  metadata_.UpdateHash();

  if (!Validate()) {
    fprintf(stderr, "Metadata is invalid");
    return ZX_ERR_BAD_STATE;
  }

  if (file->Seek(disk_offset, SEEK_SET) < 0) {
    fprintf(stderr, "Error seeking disk\n");
    return ZX_ERR_IO;
  }

  const fvm::MetadataBuffer* data = metadata_.UnsafeGetRaw();
  if (file->Write(data->data(), data->size()) != static_cast<ssize_t>(data->size())) {
    fprintf(stderr, "Error writing metadata to disk\n");
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void FvmInfo::CheckValid() const {
  if (!valid_) {
    fprintf(stderr, "Error: FVM is invalid\n");
    exit(-1);
  }
}

zx_status_t FvmInfo::Grow(const fvm::Header& dimensions) {
  CheckValid();
  bool needs_grow =
      dimensions.GetAllocationTableUsedEntryCount() >
          SuperBlock().GetAllocationTableUsedEntryCount() ||
      dimensions.GetAllocationTableAllocatedEntryCount() >
          SuperBlock().GetAllocationTableAllocatedEntryCount() ||
      dimensions.GetPartitionTableEntryCount() > SuperBlock().GetPartitionTableEntryCount() ||
      dimensions.fvm_partition_size > SuperBlock().fvm_partition_size;
  if (!needs_grow) {
    return ZX_OK;
  }
  auto metadata_or = metadata_.CopyWithNewDimensions(dimensions);
  if (metadata_or.is_error()) {
    fprintf(stderr, "Failed to copy metadata: %d\n", metadata_or.status_value());
    return metadata_or.status_value();
  }

  metadata_ = std::move(metadata_or.value());
  return ZX_OK;
}

zx_status_t FvmInfo::GrowForSlices(size_t slice_count) {
  fvm::Header dimensions = fvm::Header::FromSliceCount(
      fvm::kMaxUsablePartitions, (pslice_hint_ - 1) + slice_count, SliceSize());
  return Grow(dimensions);
}

zx_status_t FvmInfo::AllocatePartition(const fvm::PartitionDescriptor* partition, uint8_t* guid,
                                       uint32_t* vpart_index) {
  CheckValid();
  for (unsigned index = vpart_hint_; index < SuperBlock().GetPartitionTableEntryCount(); index++) {
    zx_status_t status;
    fvm::VPartitionEntry* vpart = nullptr;
    if ((status = GetPartition(index, &vpart)) != ZX_OK) {
      fprintf(stderr, "Failed to retrieve partition %u\n", index);
      return status;
    }

    // Make sure this vpartition has not already been allocated
    if (vpart->IsFree()) {
      *vpart = fvm::VPartitionEntry::Create(
          partition->type, guid, 0, fvm::VPartitionEntry::Name(partition->name), partition->flags);
      vpart_hint_ = index + 1;
      dirty_ = true;
      *vpart_index = index;
      return ZX_OK;
    }
  }

  fprintf(stderr, "Unable to find any free partitions (last allocated %u, avail %lu)\n",
          vpart_hint_, SuperBlock().GetPartitionTableEntryCount());
  return ZX_ERR_INTERNAL;
}

zx_status_t FvmInfo::AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice) {
  CheckValid();
  const fvm::Header& sb = SuperBlock();

  for (uint32_t index = pslice_hint_; index <= sb.GetAllocationTableUsedEntryCount(); index++) {
    fvm::SliceEntry* slice = nullptr;
    if (zx_status_t status = GetSlice(index, &slice); status != ZX_OK) {
      fprintf(stderr, "Failed to retrieve slice %u\n", index);
      return status;
    }

    if (slice->IsAllocated()) {
      continue;
    }

    pslice_hint_ = index + 1;

    fvm::VPartitionEntry* partition;
    if (zx_status_t status = GetPartition(vpart, &partition); status != ZX_OK) {
      return status;
    }

    slice->Set(vpart, vslice);
    partition->slices++;

    dirty_ = true;
    *pslice = index;
    return ZX_OK;
  }

  fprintf(stderr, "Unable to find any free slices (last allocated %u, avail %lu)\n", pslice_hint_,
          sb.GetAllocationTableUsedEntryCount());
  return ZX_ERR_INTERNAL;
}

zx_status_t FvmInfo::GetPartition(size_t index, fvm::VPartitionEntry** out) const {
  CheckValid();
  const fvm::Header& header = SuperBlock();

  if (index < 1 || index > header.GetPartitionTableEntryCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *out = &metadata_.GetPartitionEntry(fvm::SuperblockType::kPrimary, index);
  return ZX_OK;
}

zx_status_t FvmInfo::GetSlice(size_t index, fvm::SliceEntry** out) const {
  CheckValid();
  const fvm::Header& header = SuperBlock();

  if (index < 1 || index > header.GetAllocationTableUsedEntryCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *out = &metadata_.GetSliceEntry(fvm::SuperblockType::kPrimary, index);
  return ZX_OK;
}

const fvm::Header& FvmInfo::SuperBlock() const {
  return metadata_.GetHeader(fvm::SuperblockType::kPrimary);
}
