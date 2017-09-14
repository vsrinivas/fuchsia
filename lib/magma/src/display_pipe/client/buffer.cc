// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <utility>

#include <zx/vmar.h>
#include <zx/vmo.h>

#include "buffer.h"

Buffer *Buffer::NewBuffer(uint32_t width, uint32_t height) {
    uint64_t buffer_size = width * height * 4;
    zx::vmo vmo;
    zx_status_t err = zx::vmo::create(buffer_size, 0, &vmo);
    if (err != ZX_OK) {
        printf("Can't create %ld bytes vmo.\n", buffer_size);
        return nullptr;
    }

    zx::event acquire_fence;
    err = zx::event::create(0, &acquire_fence);
    if (err != ZX_OK) {
        printf("Failed to create acquire_fence.\n");
        return nullptr;
    }

    zx::event release_fence;
    err = zx::event::create(0, &release_fence);
    if (err != ZX_OK) {
        printf("Failed to create release_fence.\n");
        return nullptr;
    }
    release_fence.signal(0, ZX_EVENT_SIGNALED);

    uintptr_t ptr;
    err = zx::vmar::root_self().map(
        0, vmo, 0, buffer_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
        &ptr);
    if (err != ZX_OK) {
        printf("Can't map vmo.\n");
        return nullptr;
    }

    Buffer *b = new Buffer();

    b->vmo_ = std::move(vmo);
    b->pixels_ = reinterpret_cast<uint32_t *>(ptr);
    b->size_ = buffer_size;
    b->width_ = width;
    b->height_ = height;

    b->acquire_fence_ = std::move(acquire_fence);
    b->release_fence_ = std::move(release_fence);

    return b;
}

Buffer::~Buffer() {
    zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(pixels_), size_);
}

void Buffer::Fill(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0xff << 24 | r << 16 | g << 8 | b;
    for (unsigned int i = 0;
         i < width_ * height_; i++) {
        pixels_[i] = color;
    }

    // The zircon kernel has a bug where it does a full cache flush for every
    // page.  ZX-806.
    // TODO(MA-277): Replace the hard coded 4096 with size_ once the above bug
    // is fixed.
    vmo_.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, 4096, nullptr, 0);
}

void Buffer::Reset() {
    acquire_fence_.signal(ZX_EVENT_SIGNALED, 0);
    release_fence_.signal(ZX_EVENT_SIGNALED, 0);
}

void Buffer::Signal() {
    acquire_fence_.signal(0, ZX_EVENT_SIGNALED);
}
