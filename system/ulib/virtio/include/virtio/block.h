// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS

typedef struct virtio_blk_geometry {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
} __PACKED virtio_blk_geometry_t;

typedef struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    virtio_blk_geometry_t geometry;
    uint32_t blk_size;
} __PACKED virtio_blk_config_t;

__END_CDECLS
