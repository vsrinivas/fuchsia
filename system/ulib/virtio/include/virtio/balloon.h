// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

// clang-format off
#define VIRTIO_BALLOON_F_MUST_TELL_HOST   (1u << 0)
#define VIRTIO_BALLOON_F_STATS_VQ         (1u << 1)
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM   (1u << 2)

#define VIRTIO_BALLOON_S_SWAP_IN          0
#define VIRTIO_BALLOON_S_SWAP_OUT         1
#define VIRTIO_BALLOON_S_MAJFLT           2
#define VIRTIO_BALLOON_S_MINFLT           3
#define VIRTIO_BALLOON_S_MEMFREE          4
#define VIRTIO_BALLOON_S_MEMTOT           5
// This one isn't in the 1.0 Spec, but is defined by linux to be how big the
// balloon can be inflated  without pushing the guest system to swap.
#define VIRTIO_BALLOON_S_AVAIL            6
// clang-format on

__BEGIN_CDECLS

typedef struct virtio_balloon_stat {
    uint16_t tag;
    uint64_t val;
} __PACKED virtio_balloon_stat_t;

typedef struct virtio_balloon_config {
    uint32_t num_pages;
    uint32_t actual;
} __PACKED virtio_balloon_config_t;

__END_CDECLS
