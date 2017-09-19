// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "container.h"

zx_status_t Container::Create(const char* path, fbl::unique_ptr<Container>* container) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("Unable to open path %s\n", path);
        return -1;
    }

    uint8_t data[HEADER_SIZE];
    if (lseek(fd, 0, SEEK_SET) < 0) {
        printf("Error seeking block device\n");
        close(fd);
        return -1;
    }

    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Error reading block device\n");
        close(fd);
        return -1;
    }

    close(fd);

    fbl::AllocChecker ac;

    if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
        fvm::fvm_t* sb = reinterpret_cast<fvm::fvm_t*>(data);

        // Found fvm container
        fbl::unique_ptr<Container> fvmContainer(new (&ac) FvmContainer(path, sb->slice_size));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *container = fbl::move(fvmContainer);
        return ZX_OK;
    }

    fvm::sparse_image_t* image = reinterpret_cast<fvm::sparse_image_t*>(data);
    if (image->magic == fvm::kSparseFormatMagic) {
        // Found sparse container
        fbl::unique_ptr<Container> sparseContainer(new (&ac) SparseContainer(path,
                                                                             image->slice_size));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *container = fbl::move(sparseContainer);
        return ZX_OK;
    }

    printf("File format not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
}