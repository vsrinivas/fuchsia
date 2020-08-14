// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm-host/sparse-paver.h"

#include <algorithm>
#include <memory>
zx_status_t SparsePaver::Create(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t slice_size,
                                size_t disk_offset, size_t disk_size,
                                std::unique_ptr<SparsePaver>* out) {
  std::unique_ptr<SparsePaver> paver(new SparsePaver(disk_offset, disk_size));

  zx_status_t status = paver->Init(std::move(wrapper), slice_size);

  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(paver);
  return status;
}

zx_status_t SparsePaver::AddPartition(const SparsePartitionInfo* partition,
                                      fvm::SparseReader* reader) {
  info_.CheckValid();

  // Assign random guid.
  uint8_t guid[fvm::kGuidSize];
  static unsigned int seed = static_cast<unsigned int>(time(0));
  for (size_t i = 0; i < fvm::kGuidSize; i++) {
    guid[i] = static_cast<uint8_t>(rand_r(&seed));
  }

  uint32_t vpart_index;
  const fvm::PartitionDescriptor* descriptor = &partition->descriptor;
  zx_status_t status = info_.AllocatePartition(descriptor, guid, &vpart_index);
  if (status != ZX_OK) {
    return status;
  }

  // Allocate all slices for this partition.
  for (uint32_t i = 0; i < descriptor->extent_count; i++) {
    status = AddExtent(vpart_index, &partition->extents[i], reader);
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t SparsePaver::Commit() {
  info_.CheckValid();

  if (disk_ptr_ > disk_offset_ + disk_size_) {
    fprintf(stderr, "FVM metadata size exceeds disk size\n");
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = info_.Write(file_.get(), disk_offset_, disk_size_);

  if (status != ZX_OK) {
    return status;
  }

  // Move pointer to the end of the designated partition size to prevent any further edits.
  disk_ptr_ = disk_offset_ + disk_size_ + 1;
  file_->Sync();
  return ZX_OK;
}

zx_status_t SparsePaver::Init(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t slice_size) {
  file_ = std::move(wrapper);
  zx_status_t status = info_.Reset(disk_size_, slice_size);
  if (status != ZX_OK) {
    return status;
  }

  disk_ptr_ = disk_offset_ + info_.MetadataSize() * 2;
  if (disk_ptr_ >= disk_offset_ + disk_size_) {
    fprintf(stderr, "FVM metadata size exceeds disk size\n");
    return ZX_ERR_INTERNAL;
  }

  off_t result = file_->Seek(disk_ptr_, SEEK_SET);
  if (result < 0 || static_cast<size_t>(result) != disk_ptr_) {
    return ZX_ERR_IO;
  }

  data_.reset(new uint8_t[info_.SliceSize()]);
  return ZX_OK;
}

zx_status_t SparsePaver::AddExtent(uint32_t vpart_index, fvm::ExtentDescriptor* extent,
                                   fvm::SparseReader* reader) {
  uint32_t pslice_start = 0;
  uint32_t pslice_total = 0;

  size_t bytes_left = extent->extent_length;
  if (extent->slice_start > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INTERNAL;
  }
  uint32_t vslice = static_cast<uint32_t>(extent->slice_start);

  for (unsigned i = 0; i < extent->slice_count; i++) {
    uint32_t pslice;

    zx_status_t status = info_.AllocateSlice(vpart_index, vslice + i, &pslice);
    if (status != ZX_OK) {
      return status;
    }

    if (!pslice_start) {
      pslice_start = pslice;
    }

    // On a new FVM container, pslice allocation is expected to be contiguous.
    if (pslice != pslice_start + pslice_total) {
      fprintf(stderr, "fvm: pslice allocation unexpectedly non-contiguous (internal error)\n");
      return ZX_ERR_INTERNAL;
    }

    if ((status = WriteSlice(&bytes_left, reader)) != ZX_OK) {
      return status;
    }

    pslice_total++;
  }

  return ZX_OK;
}

zx_status_t SparsePaver::WriteSlice(size_t* bytes_left, fvm::SparseReader* reader) {
  info_.CheckValid();
  const size_t slice_size = info_.SliceSize();

  if (disk_ptr_ + slice_size > disk_offset_ + disk_size_) {
    fprintf(stderr, "Partition data exceeds the provided disk size\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t read_length = std::min(slice_size, *bytes_left);

  if (read_length > 0) {
    size_t bytes_read = 0;
    zx_status_t status = reader->ReadData(data_.get(), read_length, &bytes_read);
    if (status != ZX_OK) {
      return status;
    }

    if (bytes_read < read_length) {
      fprintf(stderr, "Slice data is less than expected\n");
      return ZX_ERR_IO;
    }

    if (read_length < slice_size) {
      memset(&data_[read_length], 0, slice_size - read_length);
    }

    *bytes_left -= bytes_read;
  } else {
    memset(data_.get(), 0, slice_size);
  }

  ssize_t result = file_->Write(data_.get(), slice_size);
  if (result < 0 || static_cast<size_t>(result) != slice_size) {
    return ZX_ERR_IO;
  }

  disk_ptr_ += slice_size;
  return ZX_OK;
}
