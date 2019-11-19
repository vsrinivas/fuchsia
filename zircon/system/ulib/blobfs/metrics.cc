// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>

#include <fs/trace.h>

namespace blobfs {
namespace {

// Time between each Cobalt flush.
constexpr zx::duration kCobaltFlushTimer = zx::min(5);

size_t TicksToMs(const zx::ticks& ticks) { return fzl::TicksToNs(ticks) / zx::msec(1); }

}  // namespace

BlobfsMetrics::~BlobfsMetrics() { Dump(); }

cobalt_client::CollectorOptions BlobfsMetrics::GetBlobfsOptions() {
  cobalt_client::CollectorOptions options = cobalt_client::CollectorOptions::GeneralAvailability();
  // Filesystems project name as defined in cobalt-analytics projects.yaml.
  options.project_name = "local_storage";
  return options;
}

void BlobfsMetrics::Dump() const {
  if (!Collecting()) {
    return;
  }
  constexpr uint64_t mb = 1 << 20;

  FS_TRACE_INFO("Allocation Info:\n");
  FS_TRACE_INFO("  Allocated %zu blobs (%zu MB) in %zu ms\n", blobs_created_,
                blobs_created_total_size_ / mb, TicksToMs(total_allocation_time_ticks_));
  FS_TRACE_INFO("Writeback Info:\n");
  FS_TRACE_INFO("  (Client) Wrote %zu MB of data and %zu MB of merkle trees\n",
                data_bytes_written_ / mb, merkle_bytes_written_ / mb);
  FS_TRACE_INFO("  (Client) Enqueued writeback in %zu ms, made merkle tree in %zu ms\n",
                TicksToMs(total_write_enqueue_time_ticks_),
                TicksToMs(total_merkle_generation_time_ticks_));
  FS_TRACE_INFO("  (Writeback Thread) Wrote %zu MB of data in %zu ms\n",
                total_writeback_bytes_written_ / mb, TicksToMs(total_writeback_time_ticks_));
  FS_TRACE_INFO("Lookup Info:\n");
  FS_TRACE_INFO("  Opened %zu blobs (%zu MB)\n", blobs_opened_, blobs_opened_total_size_ / mb);
  FS_TRACE_INFO("  Verified %zu blobs (%zu MB data, %zu MB merkle)\n", blobs_verified_,
                blobs_verified_total_size_data_ / mb, blobs_verified_total_size_merkle_ / mb);
  FS_TRACE_INFO("  Spent %zu ms reading %zu MB from disk, %zu ms verifying\n",
                TicksToMs(total_read_from_disk_time_ticks_), bytes_read_from_disk_ / mb,
                TicksToMs(total_verification_time_ticks_));
}

void BlobfsMetrics::ScheduleMetricFlush() {
  async::PostDelayedTask(
      flush_loop_.dispatcher(),
      [this]() {
        mutable_collector()->Flush();
        ScheduleMetricFlush();
      },
      kCobaltFlushTimer);
}

void BlobfsMetrics::Collect() {
  cobalt_metrics_.EnableMetrics(true);
  // TODO(gevalentino): Once we have async llcpp bindings, instead pass a dispatcher for
  // handling collector IPCs.
  flush_loop_.StartThread("blobfs-metric-flusher");
  ScheduleMetricFlush();
}

void BlobfsMetrics::UpdateAllocation(uint64_t size_data, const fs::Duration& duration) {
  if (Collecting()) {
    blobs_created_++;
    blobs_created_total_size_ += size_data;
    total_allocation_time_ticks_ += duration;
  }
}

void BlobfsMetrics::UpdateLookup(uint64_t size) {
  if (Collecting()) {
    blobs_opened_++;
    blobs_opened_total_size_ += size;
  }
}

void BlobfsMetrics::UpdateClientWrite(uint64_t data_size, uint64_t merkle_size,
                                      const fs::Duration& enqueue_duration,
                                      const fs::Duration& generate_duration) {
  if (Collecting()) {
    data_bytes_written_ += data_size;
    merkle_bytes_written_ += merkle_size;
    total_write_enqueue_time_ticks_ += enqueue_duration;
    total_merkle_generation_time_ticks_ += generate_duration;
  }
}

void BlobfsMetrics::UpdateWriteback(uint64_t size, const fs::Duration& duration) {
  if (Collecting()) {
    total_writeback_time_ticks_ += duration;
    total_writeback_bytes_written_ += size;
  }
}

void BlobfsMetrics::UpdateMerkleDiskRead(uint64_t size, const fs::Duration& duration) {
  if (Collecting()) {
    total_read_from_disk_time_ticks_ += duration;
    bytes_read_from_disk_ += size;
  }
}

void BlobfsMetrics::UpdateMerkleDecompress(uint64_t size_compressed, uint64_t size_uncompressed,
                                           const fs::Duration& read_duration,
                                           const fs::Duration& decompress_duration) {
  if (Collecting()) {
    bytes_compressed_read_from_disk_ += size_compressed;
    bytes_decompressed_from_disk_ += size_uncompressed;
    total_read_compressed_time_ticks_ += read_duration;
    total_decompress_time_ticks_ += decompress_duration;
  }
}

void BlobfsMetrics::UpdateMerkleVerify(uint64_t size_data, uint64_t size_merkle,
                                       const fs::Duration& duration) {
  if (Collecting()) {
    blobs_verified_++;
    blobs_verified_total_size_data_ += size_data;
    blobs_verified_total_size_merkle_ += size_merkle;
    total_verification_time_ticks_ += duration;
  }
}

}  // namespace blobfs
