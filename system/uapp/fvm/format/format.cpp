// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format.h"

zx_status_t Format::Create(const char* path, const char* type, fbl::unique_ptr<Format>* out) {
    fbl::unique_fd fd(open(path, O_RDONLY));
    if (!fd) {
        fprintf(stderr, "Format::Create: Could not open %s\n", path);
        return ZX_ERR_IO;
    }

    uint8_t data[HEADER_SIZE];
    if (lseek(fd.get(), 0, SEEK_SET) < 0) {
        printf("Error seeking block device\n");
        return -1;
    }

    if (read(fd.get(), data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Error reading block device\n");
        return -1;
    }

    fbl::AllocChecker ac;
    if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        // Found minfs partition
        fbl::unique_ptr<Format> minfsFormat(new (&ac) MinfsFormat(fbl::move(fd), type));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *out = fbl::move(minfsFormat);
        return ZX_OK;
    } else if (!memcmp(data, blobstore_magic, sizeof(blobstore_magic))) {
        // Found blobstore partition
        fbl::unique_ptr<Format> blobfsFormat(new (&ac) BlobfsFormat(fbl::move(fd), type));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *out = fbl::move(blobfsFormat);
        return ZX_OK;
    }

    printf("Disk format not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
}
