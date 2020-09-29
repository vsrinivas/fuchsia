// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm-host/container.h"

#include <inttypes.h>

#include <memory>
#include <utility>

#include <fbl/unique_fd.h>

zx_status_t Container::Create(const char* path, off_t offset, uint32_t flags,
                              std::unique_ptr<Container>* container) {
  if ((flags & ~fvm::kSparseFlagAllValid) != 0) {
    fprintf(stderr, "Invalid flags: %08" PRIx32 "\n", flags);
    return -1;
  }

  fbl::unique_fd fd(open(path, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Unable to open path %s\n", path);
    return -1;
  }

  uint8_t data[HEADER_SIZE];
  if (lseek(fd.get(), offset, SEEK_SET) < 0) {
    fprintf(stderr, "Error seeking block device\n");
    return -1;
  }

  if (read(fd.get(), data, sizeof(data)) != sizeof(data)) {
    fprintf(stderr, "Error reading block device\n");
    return -1;
  }

  if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
    // Found fvm container
    std::unique_ptr<FvmContainer> fvmContainer;
    zx_status_t status = FvmContainer::CreateExisting(path, offset, &fvmContainer);
    if (status != ZX_OK) {
      return status;
    }

    *container = std::move(fvmContainer);
    return ZX_OK;
  }

  fvm::SparseImage* image = reinterpret_cast<fvm::SparseImage*>(data);
  if (image->magic == fvm::kSparseFormatMagic) {
    if (offset > 0) {
      fprintf(stderr, "Invalid offset for sparse file\n");
      return ZX_ERR_INVALID_ARGS;
    }

    // Found sparse container
    std::unique_ptr<SparseContainer> sparseContainer;
    zx_status_t status = SparseContainer::CreateExisting(path, &sparseContainer);
    if (status != ZX_OK) {
      return status;
    }

    *container = std::move(sparseContainer);
    return ZX_OK;
  }

  fprintf(stderr, "File format not supported\n");
  return ZX_ERR_NOT_SUPPORTED;
}

Container::Container(const char* path, size_t slice_size, uint32_t flags)
    : slice_size_(slice_size), flags_(flags) {
  path_.AppendPrintf("%s", path);
}

Container::~Container() {}

uint64_t Container::CalculateDiskSizeForSlices(size_t slice_count) const {
  uint64_t data_size = slice_count * slice_size_;
  uint64_t total_size = 0;
  size_t metadata_size = 0;

  // This loop will necessarily break at some point. The re-calculation of total_size and
  // metadata_size will cause both of these values to increase, except on the last iteration
  // where metadata_size will not change, causing the loop condition to become false.
  // metadata_size *must* stop increasing at some point, since the data_size is always a fixed
  // value, and the metadata by itself will not grow fast enough to necessitate its continued
  // growth at the same or higher rate.
  // As an example, with the current metadata size calculation and a slice size of 8192 bytes,
  // around ~8mb of disk space will require metadata growth of only ~8kb. Even if the
  // metadata_size grows very quickly at first, this amount will diminish very quickly until it
  // reaches 0.
  do {
    total_size = data_size + (metadata_size * 2);
    metadata_size = fvm::MetadataSizeForDiskSize(total_size, slice_size_);
  } while (total_size - (metadata_size * 2) < data_size);

  return total_size;
}
