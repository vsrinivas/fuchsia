// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include <stdio.h>

#include <blobfs/metrics.h>
#include <fs/trace.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>

namespace blobfs {
namespace {

size_t TicksToMs(const zx::ticks& ticks) {
    return fzl::TicksToNs(ticks) / zx::msec(1);
}

} // namespace

void BlobfsMetrics::Dump() const {
    constexpr uint64_t mb = 1 << 20;

    FS_TRACE_INFO("Allocation Info:\n");
    FS_TRACE_INFO("  Allocated %zu blobs (%zu MB) in %zu ms\n", blobs_created,
                  blobs_created_total_size / mb,
                  TicksToMs(total_allocation_time_ticks));
    FS_TRACE_INFO("Writeback Info:\n");
    FS_TRACE_INFO("  (Client) Wrote %zu MB of data and %zu MB of merkle trees\n",
                  data_bytes_written / mb, merkle_bytes_written / mb);
    FS_TRACE_INFO("  (Client) Enqueued writeback in %zu ms, made merkle tree in %zu ms\n",
                  TicksToMs(total_write_enqueue_time_ticks),
                  TicksToMs(total_merkle_generation_time_ticks));
    FS_TRACE_INFO("  (Writeback Thread) Wrote %zu MB of data in %zu ms\n",
                  total_writeback_bytes_written / mb,
                  TicksToMs(total_writeback_time_ticks));
    FS_TRACE_INFO("Lookup Info:\n");
    FS_TRACE_INFO("  Opened %zu blobs (%zu MB)\n", blobs_opened,
                  blobs_opened_total_size / mb);
    FS_TRACE_INFO("  Verified %zu blobs (%zu MB data, %zu MB merkle)\n",
                  blobs_verified, blobs_verified_total_size_data / mb,
                  blobs_verified_total_size_merkle / mb);
    FS_TRACE_INFO("  Spent %zu ms reading %zu MB from disk, %zu ms verifying\n",
                  TicksToMs(total_read_from_disk_time_ticks),
                  bytes_read_from_disk / mb,
                  TicksToMs(total_verification_time_ticks));
}

} // namespace blobfs
