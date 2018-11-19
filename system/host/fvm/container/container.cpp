// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <fbl/unique_fd.h>

#include <utility>

#include "fvm/container.h"

zx_status_t Container::Create(const char* path, off_t offset, off_t length,
                              uint32_t flags, fbl::unique_ptr<Container>* container) {
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

    fbl::AllocChecker ac;
    if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
        fvm::fvm_t* sb = reinterpret_cast<fvm::fvm_t*>(data);

        // Found fvm container
        fbl::unique_ptr<Container> fvmContainer(new (&ac) FvmContainer(path, sb->slice_size,
                                                                       offset, length));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        *container = std::move(fvmContainer);
        return ZX_OK;
    }

    fvm::sparse_image_t* image = reinterpret_cast<fvm::sparse_image_t*>(data);
    if (image->magic == fvm::kSparseFormatMagic) {
        if (offset > 0) {
            fprintf(stderr, "Invalid offset for sparse file\n");
            return ZX_ERR_INVALID_ARGS;
        }

        // Found sparse container
        fbl::unique_ptr<Container> sparseContainer(new (&ac) SparseContainer(path,
                                                                             image->slice_size,
                                                                             flags));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *container = std::move(sparseContainer);
        return ZX_OK;
    }

    fprintf(stderr, "File format not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
}

Container::Container(const char* path, size_t slice_size, uint32_t flags)
    : dirty_(false), slice_size_(slice_size), flags_(flags) {
    snprintf(path_, sizeof(path_), "%s", path);
}

Container::~Container() {}
