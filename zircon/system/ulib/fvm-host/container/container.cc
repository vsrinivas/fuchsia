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
  return fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, slice_count, slice_size_)
      .fvm_partition_size;
}
