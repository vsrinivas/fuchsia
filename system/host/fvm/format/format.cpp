// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm/format.h"

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

zx_status_t Format::Create(const char* path, const char* type, fbl::unique_ptr<Format>* out) {
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

    fbl::AllocChecker ac;
    if (part == DISK_FORMAT_MINFS) {
        // Found minfs partition
        fbl::unique_ptr<Format> minfsFormat(new (&ac) MinfsFormat(fbl::move(fd), type));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *out = fbl::move(minfsFormat);
        return ZX_OK;
    } else if (part == DISK_FORMAT_BLOBFS) {
        // Found blobfs partition
        fbl::unique_ptr<Format> blobfsFormat(new (&ac) BlobfsFormat(fbl::move(fd), type));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *out = fbl::move(blobfsFormat);
        return ZX_OK;
    }

    fprintf(stderr, "Disk format not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Format::Check(fbl::unique_fd fd, off_t start, off_t end,
                          const fbl::Vector<size_t>& extent_lengths, disk_format_t part) {
    if (part == DISK_FORMAT_BLOBFS) {
        return blobfs::blobfs_fsck(fbl::move(fd), start, end, extent_lengths);
    } else if (part == DISK_FORMAT_MINFS) {
        return minfs::minfs_fsck(fbl::move(fd), start, end, extent_lengths);
    }

    fprintf(stderr, "Format not supported\n");
    return ZX_ERR_INVALID_ARGS;
}
