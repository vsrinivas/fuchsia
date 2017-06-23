// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

// clang-format off
#define VIRTIO_BLK_F_BARRIER    (1u << 0)
#define VIRTIO_BLK_F_SIZE_MAX   (1u << 1)
#define VIRTIO_BLK_F_SEG_MAX    (1u << 2)
#define VIRTIO_BLK_F_GEOMETRY   (1u << 4)
#define VIRTIO_BLK_F_RO         (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE   (1u << 6)
#define VIRTIO_BLK_F_SCSI       (1u << 7)
#define VIRTIO_BLK_F_FLUSH      (1u << 9)
#define VIRTIO_BLK_F_TOPOLOGY   (1u << 10)
#define VIRTIO_BLK_F_CONFIG_WCE (1u << 11)

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2
// clang-format on

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

typedef struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __PACKED virtio_blk_req_t;

__END_CDECLS
