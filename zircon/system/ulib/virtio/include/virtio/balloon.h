// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIRTIO_BALLOON_H_
#define VIRTIO_BALLOON_H_

#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off
#define VIRTIO_BALLOON_F_MUST_TELL_HOST   (1u << 0)
#define VIRTIO_BALLOON_F_STATS_VQ         (1u << 1)
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM   (1u << 2)
#define VIRTIO_BALLOON_F_FREE_PAGE_HINT   (1u << 3)
#define VIRTIO_BALLOON_F_PAGE_POISON      (1u << 4)
#define VIRTIO_BALLOON_F_PAGE_REPORTING   (1u << 5)

#define VIRTIO_BALLOON_S_SWAP_IN          0
#define VIRTIO_BALLOON_S_SWAP_OUT         1
#define VIRTIO_BALLOON_S_MAJFLT           2
#define VIRTIO_BALLOON_S_MINFLT           3
#define VIRTIO_BALLOON_S_MEMFREE          4
#define VIRTIO_BALLOON_S_MEMTOT           5
// These are not in the 1.0 Spec, but are defined by Linux.
#define VIRTIO_BALLOON_S_AVAIL            6 // Available memory as in /proc
#define VIRTIO_BALLOON_S_CACHES           7 // Disk caches
#define VIRTIO_BALLOON_S_HTLB_PGALLOC     8 // HugeTLB page allocations
#define VIRTIO_BALLOON_S_HTLB_PGFAIL      9 // HugeTLB page allocation failures

// clang-format on

__BEGIN_CDECLS

typedef struct virtio_balloon_stat {
  uint16_t tag;
  uint64_t val;
} __PACKED virtio_balloon_stat_t;

typedef struct virtio_balloon_config {
  uint32_t num_pages;
  uint32_t actual;
  uint32_t free_page_hint_cmd_id;
  uint32_t poison_val;
} __PACKED virtio_balloon_config_t;

__END_CDECLS

#endif  // VIRTIO_BALLOON_H_
