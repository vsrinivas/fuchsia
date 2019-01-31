// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off

// For virtio_vsock_hdr_t.
#define VIRTIO_VSOCK_TYPE_STREAM            1u

#define VIRTIO_VSOCK_OP_INVALID             0u
#define VIRTIO_VSOCK_OP_REQUEST             1u
#define VIRTIO_VSOCK_OP_RESPONSE            2u
#define VIRTIO_VSOCK_OP_RST                 3u
#define VIRTIO_VSOCK_OP_SHUTDOWN            4u
#define VIRTIO_VSOCK_OP_RW                  5u
#define VIRTIO_VSOCK_OP_CREDIT_UPDATE       6u
#define VIRTIO_VSOCK_OP_CREDIT_REQUEST      7u

#define VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV     (1u << 0)
#define VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND     (1u << 1)
#define VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH     (VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV | \
                                             VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)

// For virtio_vsock_event_t.
#define VIRTIO_VSOCK_EVENT_TRANSPORT_RESET  0u

// clang-format on

__BEGIN_CDECLS;

typedef struct virtio_vsock_config {
    uint64_t guest_cid;
} __PACKED virtio_vsock_config_t;

typedef struct virtio_vsock_hdr {
    uint64_t src_cid;
    uint64_t dst_cid;
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t len;
    uint16_t type;
    uint16_t op;
    uint32_t flags;
    uint32_t buf_alloc;
    uint32_t fwd_cnt;
} __PACKED virtio_vsock_hdr_t;

typedef struct virtio_vsock_event {
    uint32_t id;
} __PACKED virtio_vsock_event_t;

__END_CDECLS;
