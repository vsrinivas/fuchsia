// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fvm-host/format.h>
#include <fvm-host/fvm-info.h>
#include <fvm/fvm.h>

zx_status_t FvmInfo::Reset(size_t disk_size, size_t slice_size) {
  valid_ = false;

  if (slice_size == 0) {
    fprintf(stderr, "Invalid slice size\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // Even if disk size is 0, this will default to at least fvm::kBlockSize
  metadata_size_ = fvm::MetadataSizeForDiskSize(disk_size, slice_size);
  metadata_.reset(new uint8_t[metadata_size_ * 2]);

  // Clear entire primary copy of metadata
  memset(metadata_.get(), 0, metadata_size_);

  // Superblock
  fvm::Header* sb = SuperBlock();
  sb->magic = fvm::kMagic;
  sb->version = fvm::kVersion;
  sb->pslice_count = fvm::UsableSlicesCount(disk_size, slice_size);
  sb->slice_size = slice_size;
  sb->fvm_partition_size = disk_size;
  sb->vpartition_table_size = fvm::PartitionTableLength(fvm::kMaxVPartitions);
  sb->allocation_table_size = fvm::AllocTableLengthForDiskSize(disk_size, slice_size);
  sb->generation = 0;

  if (sb->pslice_count == 0) {
    fprintf(stderr, "No space available for slices\n");
    return ZX_ERR_NO_SPACE;
  }

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
          fvm::SlicesStart(disk_size, slice_size), fvm::UsableSlicesCount(disk_size, slice_size));

  // Copy the valid primary metadata to the secondary metadata.
  memcpy(metadata_.get() + metadata_size_, metadata_.get(), metadata_size_);

  return ZX_OK;
}

zx_status_t FvmInfo::Load(fvm::host::FileWrapper* file, uint64_t disk_offset, uint64_t disk_size) {
  uint64_t start_position = file->Tell();
  valid_ = false;

  if (disk_size == 0) {
    return ZX_OK;
  }

  // For now just reset this to the fvm header size - we can grow it to the full metadata size
  // later.
  metadata_.reset(new uint8_t[sizeof(fvm::Header)]);

  // If Container already exists, read metadata from disk.
  // Read superblock first so we can determine if container has a different slice size.
  file->Seek(disk_offset, SEEK_SET);
  ssize_t result = file->Read(metadata_.get(), sizeof(fvm::Header));
  file->Seek(start_position, SEEK_SET);

  if (result != static_cast<ssize_t>(sizeof(fvm::Header))) {
    fprintf(stderr, "Superblock read failed: expected %ld, actual %ld\n", sizeof(fvm::Header),
            result);
    return ZX_ERR_IO;
  }

  // If the image is obviously not an FVM header, bail out early.
  // Otherwise, we go through the effort of ensuring the header is
  // valid before using it.
  if (SuperBlock()->magic != fvm::kMagic) {
    return ZX_OK;
  }

  if (DiskSize() != disk_size) {
    fprintf(stderr, "Disk size does not match expected");
    return ZX_ERR_BAD_STATE;
  }

  // Recalculate metadata size.
  size_t old_slice_size = SuperBlock()->slice_size;
  size_t old_metadata_size = fvm::MetadataSizeForDiskSize(disk_size, old_slice_size);
  std::unique_ptr<uint8_t[]> old_metadata = std::make_unique<uint8_t[]>(old_metadata_size * 2);

  // Read remainder of metadata.
  file->Seek(disk_offset, SEEK_SET);
  result = file->Read(old_metadata.get(), old_metadata_size * 2);
  file->Seek(start_position, SEEK_SET);

  if (result != static_cast<ssize_t>(old_metadata_size * 2)) {
    fprintf(stderr, "Metadata read failed: expected %ld, actual %ld\n", old_metadata_size * 2,
            result);
    return ZX_ERR_IO;
  }

  metadata_size_ = old_metadata_size;
  metadata_.reset(old_metadata.release());

  if (Validate() == ZX_OK) {
    valid_ = true;
  }

  return ZX_OK;
}

zx_status_t FvmInfo::Validate() const {
  const void* primary = nullptr;
  const void* backup =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(metadata_.get()) + metadata_size_);
  zx_status_t status = fvm::ValidateHeader(metadata_.get(), backup, metadata_size_, &primary);

  if (status != ZX_OK) {
    fprintf(stderr, "Header validation failed with status %d\n", status);
    return status;
  }

  if (primary != metadata_.get()) {
    fprintf(stderr, "Can only update FVM with valid primary as first copy\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FvmInfo::Write(fvm::host::FileWrapper* file, size_t disk_offset, size_t disk_size) {
  fvm::Header* sb = SuperBlock();
  if (disk_size != sb->fvm_partition_size) {
    // If disk size has changed, update and attempt to grow metadata.
    sb->pslice_count = fvm::UsableSlicesCount(disk_size, SliceSize());
    sb->fvm_partition_size = disk_size;
    sb->allocation_table_size = fvm::AllocTableLengthForDiskSize(disk_size, SliceSize());

    size_t new_metadata_size = fvm::MetadataSizeForDiskSize(disk_size, SliceSize());
    zx_status_t status = Grow(new_metadata_size);
    if (status != ZX_OK) {
      return status;
    }
  }

  fvm::UpdateHash(metadata_.get(), metadata_size_);
  memcpy(metadata_.get() + metadata_size_, metadata_.get(), metadata_size_);

  if (Validate() != ZX_OK) {
    fprintf(stderr, "Metadata is invalid");
    return ZX_ERR_BAD_STATE;
  }

  if (file->Seek(disk_offset, SEEK_SET) < 0) {
    fprintf(stderr, "Error seeking disk\n");
    return ZX_ERR_IO;
  }

  if (file->Write(metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
    fprintf(stderr, "Error writing metadata to disk\n");
    return ZX_ERR_IO;
  }

  if (file->Write(metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
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

zx_status_t FvmInfo::Grow(size_t new_size) {
  if (new_size <= metadata_size_) {
    return ZX_OK;
  }

  xprintf("Growing metadata from %zu to %zu\n", metadata_size_, new_size);
  std::unique_ptr<uint8_t[]> new_metadata(new uint8_t[new_size * 2]);

  memcpy(new_metadata.get(), metadata_.get(), metadata_size_);
  memset(new_metadata.get() + metadata_size_, 0, new_size);
  memcpy(new_metadata.get() + new_size, metadata_.get(), metadata_size_);

  metadata_.reset(new_metadata.release());
  metadata_size_ = new_size;
  return ZX_OK;
}

zx_status_t FvmInfo::GrowForSlices(size_t slice_count) {
  size_t required_size = SuperBlock()->GetAllocationTableOffset() +
                         (pslice_hint_ + slice_count) * sizeof(fvm::SliceEntry);
  return Grow(required_size);
}

zx_status_t FvmInfo::AllocatePartition(const fvm::PartitionDescriptor* partition, uint8_t* guid,
                                       uint32_t* vpart_index) {
  CheckValid();
  for (unsigned index = vpart_hint_; index < fvm::kMaxVPartitions; index++) {
    zx_status_t status;
    fvm::VPartitionEntry* vpart = nullptr;
    if ((status = GetPartition(index, &vpart)) != ZX_OK) {
      fprintf(stderr, "Failed to retrieve partition %u\n", index);
      return status;
    }

    // Make sure this vpartition has not already been allocated
    if (vpart->slices == 0) {
      *vpart = fvm::VPartitionEntry::Create(
          partition->type, guid, 0, fvm::VPartitionEntry::Name(partition->name), partition->flags);
      vpart_hint_ = index + 1;
      dirty_ = true;
      *vpart_index = index;
      return ZX_OK;
    }
  }

  fprintf(stderr, "Unable to find any free partitions\n");
  return ZX_ERR_INTERNAL;
}

zx_status_t FvmInfo::AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice) {
  CheckValid();
  fvm::Header* sb = SuperBlock();

  for (uint32_t index = pslice_hint_; index <= sb->pslice_count; index++) {
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

  fprintf(stderr, "Unable to find any free slices\n");
  return ZX_ERR_INTERNAL;
}

zx_status_t FvmInfo::GetPartition(size_t index, fvm::VPartitionEntry** out) const {
  CheckValid();
  const fvm::Header* header = SuperBlock();

  if (index < 1 || index > header->GetPartitionTableEntryCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
  uintptr_t offset = static_cast<uintptr_t>(header->GetPartitionEntryOffset(index));
  *out = reinterpret_cast<fvm::VPartitionEntry*>(metadata_start + offset);
  return ZX_OK;
}

zx_status_t FvmInfo::GetSlice(size_t index, fvm::SliceEntry** out) const {
  CheckValid();
  const fvm::Header* header = SuperBlock();

  if (index < 1 || index > header->GetAllocationTableUsedEntryCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
  uintptr_t offset = static_cast<uintptr_t>(header->GetSliceEntryOffset(index));
  *out = reinterpret_cast<fvm::SliceEntry*>(metadata_start + offset);
  return ZX_OK;
}

fvm::Header* FvmInfo::SuperBlock() const {
  return static_cast<fvm::Header*>((void*)metadata_.get());
}
