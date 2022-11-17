// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIRTIO_MEM_H_
#define VIRTIO_MEM_H_

#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off
#define VIRTIO_MEM_F_ACPI_PXM                 (1u << 0)
#define VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE   (1u << 1)

#define VIRTIO_MEM_REQ_PLUG               0
#define VIRTIO_MEM_REQ_UNPLUG             1
#define VIRTIO_MEM_REQ_UNPLUG_ALL         2
#define VIRTIO_MEM_REQ_STATE              3

#define VIRTIO_MEM_RESP_ACK               0
#define VIRTIO_MEM_RESP_NACK              1
#define VIRTIO_MEM_RESP_BUSY              2
#define VIRTIO_MEM_RESP_ERROR             3

#define VIRTIO_MEM_STATE_PLUGGED          0
#define VIRTIO_MEM_STATE_UNPLUGGED        1
#define VIRTIO_MEM_STATE_MIXED            2
// clang-format on

__BEGIN_CDECLS

typedef struct virtio_mem_req_plug {
  uint64_t addr;
  uint16_t nb_blocks;
  uint16_t padding[3];
} __PACKED virtio_mem_req_plug_t;

typedef struct virtio_mem_req_unplug {
  uint64_t addr;
  uint16_t nb_blocks;
  uint16_t padding[3];
} __PACKED virtio_mem_req_unplug_t;

typedef struct virtio_mem_req_state {
  uint64_t addr;
  uint16_t nb_blocks;
  uint16_t padding[3];
} __PACKED virtio_mem_req_state_t;

typedef struct virtio_mem_req {
  uint16_t type;
  uint16_t padding[3];
  union {
    struct virtio_mem_req_plug plug;
    struct virtio_mem_req_unplug unplug;
    struct virtio_mem_req_state state;
  } u;
} __PACKED virtio_mem_req_t;

typedef struct virtio_mem_resp_state {
  uint16_t type;
} __PACKED virtio_mem_resp_state_t;

typedef struct virtio_mem_resp {
  uint16_t type;
  uint16_t padding[3];
  union {
    struct virtio_mem_resp_state state;
  } u;
} __PACKED virtio_mem_resp_t;

typedef struct virtio_mem_config {
  uint64_t block_size;
  uint16_t node_id;
  uint8_t padding[6];
  uint64_t addr;
  uint64_t region_size;
  uint64_t usable_region_size;
  uint64_t plugged_size;
  uint64_t requested_size;
} __PACKED virtio_mem_config_t;

__END_CDECLS

#endif  // VIRTIO_MEM_H_
