// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/host/format.h"

#include <memory>
#include <utility>

#include "src/storage/fvm/host/blobfs_format.h"
#include "src/storage/fvm/host/minfs_format.h"

Format::Format() : fvm_ready_(false), vpart_index_(0), flags_(0) {}

zx_status_t Format::Detect(int fd, off_t offset, disk_format_t* out) {
  uint8_t data[HEADER_SIZE];
  if (lseek(fd, offset, SEEK_SET) < 0) {
    fprintf(stderr, "Error seeking block device\n");
    return ZX_ERR_IO;
  }

  if (read(fd, data, sizeof(data)) != sizeof(data)) {
    fprintf(stderr, "Error reading block device\n");
    return ZX_ERR_IO;
  }

  if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
    *out = DISK_FORMAT_MINFS;
  } else if (!memcmp(data, blobfs_magic, sizeof(blobfs_magic))) {
    *out = DISK_FORMAT_BLOBFS;
  } else {
    *out = DISK_FORMAT_UNKNOWN;
  }

  return ZX_OK;
}

zx_status_t Format::Create(const char* path, const char* type, std::unique_ptr<Format>* out) {
  fbl::unique_fd fd(open(path, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Format::Create: Could not open %s\n", path);
    return ZX_ERR_IO;
  }

  zx_status_t status;
  disk_format_t part;
  if ((status = Detect(fd.get(), 0, &part)) != ZX_OK) {
    return status;
  }

  if (part == DISK_FORMAT_MINFS) {
    // Found minfs partition
    std::unique_ptr<Format> minfsFormat(new MinfsFormat(std::move(fd), type));
    *out = std::move(minfsFormat);
    return ZX_OK;
  } else if (part == DISK_FORMAT_BLOBFS) {
    // Found blobfs partition
    std::unique_ptr<Format> blobfsFormat(new BlobfsFormat(std::move(fd), type));
    *out = std::move(blobfsFormat);
    return ZX_OK;
  }

  fprintf(stderr, "Disk format not supported\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Format::Check(fbl::unique_fd fd, off_t start, off_t end,
                          const fbl::Vector<size_t>& extent_lengths, disk_format_t part) {
  if (part == DISK_FORMAT_BLOBFS) {
    return blobfs::blobfs_fsck(std::move(fd), start, end, extent_lengths);
  } else if (part == DISK_FORMAT_MINFS) {
    return minfs::SparseFsck(std::move(fd), start, end, extent_lengths);
  }

  fprintf(stderr, "Format not supported\n");
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Format::UsedDataSize(const fbl::unique_fd& fd, off_t start, off_t end,
                                 const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                                 uint64_t* out_size) {
  if (part == DISK_FORMAT_BLOBFS) {
    return blobfs::UsedDataSize(fd, out_size, start, end);
  } else if (part == DISK_FORMAT_MINFS) {
    fbl::unique_fd dupfd(dup(fd.get()));
    if (!dupfd) {
      fprintf(stderr, "Failed to duplicate fd\n");
      return ZX_ERR_INTERNAL;
    }
    return minfs::SparseUsedDataSize(std::move(dupfd), start, end, extent_lengths, out_size);
  }

  fprintf(stderr, "Format not supported\n");
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Format::UsedInodes(const fbl::unique_fd& fd, off_t start, off_t end,
                               const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                               uint64_t* out_inodes) {
  if (part == DISK_FORMAT_BLOBFS) {
    return blobfs::UsedInodes(fd, out_inodes, start, end);
  } else if (part == DISK_FORMAT_MINFS) {
    fbl::unique_fd dupfd(dup(fd.get()));
    if (!dupfd) {
      fprintf(stderr, "Failed to duplicate fd\n");
      return ZX_ERR_INTERNAL;
    }
    return minfs::SparseUsedInodes(std::move(dupfd), start, end, extent_lengths, out_inodes);
  }

  fprintf(stderr, "Format not supported\n");
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Format::UsedSize(const fbl::unique_fd& fd, off_t start, off_t end,
                             const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                             uint64_t* out_size) {
  if (part == DISK_FORMAT_BLOBFS) {
    return blobfs::UsedSize(fd, out_size, start, end);
  } else if (part == DISK_FORMAT_MINFS) {
    fbl::unique_fd dupfd(dup(fd.get()));
    if (!dupfd) {
      fprintf(stderr, "Failed to duplicate fd\n");
      return ZX_ERR_INTERNAL;
    }
    return minfs::SparseUsedSize(std::move(dupfd), start, end, extent_lengths, out_size);
  }

  fprintf(stderr, "Format not supported\n");
  return ZX_ERR_INVALID_ARGS;
}
