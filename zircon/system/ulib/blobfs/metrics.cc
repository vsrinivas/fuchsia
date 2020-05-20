// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>

#include <fs/metrics/events.h>
#include <fs/trace.h>

namespace blobfs {
namespace {

// Time between each Cobalt flush.
constexpr zx::duration kCobaltFlushTimer = zx::min(5);

size_t TicksToMs(const zx::ticks& ticks) { return fzl::TicksToNs(ticks) / zx::msec(1); }

fs_metrics::CompressionFormat FormatForInode(const Inode& inode) {
  if (inode.IsCompressed()) {
    auto compression = inode.header.flags & kBlobFlagMaskAnyCompression;
    switch (compression) {
      case kBlobFlagLZ4Compressed:
        return fs_metrics::CompressionFormat::kCompressedLZ4;
      case kBlobFlagZSTDCompressed:
        return fs_metrics::CompressionFormat::kCompressedZSTD;
      case kBlobFlagZSTDSeekableCompressed:
        return fs_metrics::CompressionFormat::kCompressedZSTDSeekable;
      case kBlobFlagChunkCompressed:
        return fs_metrics::CompressionFormat::kCompressedZSTDChunked;
      default:
        return fs_metrics::CompressionFormat::kUnknown;
    }
  } else {
    return fs_metrics::CompressionFormat::kUncompressed;
  }
}

}  // namespace

BlobfsMetrics::~BlobfsMetrics() { Dump(); }

void BlobfsMetrics::Dump() {
  constexpr uint64_t mb = 1 << 20;

  // Timings are only recorded when Cobalt metrics are enabled.

  FS_TRACE_INFO("Allocation Info:\n");
  FS_TRACE_INFO("  Allocated %zu blobs (%zu MB)\n", blobs_created_,
                blobs_created_total_size_ / mb);
  if (Collecting())
    FS_TRACE_INFO("  Total allocation time is %zu ms\n", TicksToMs(total_allocation_time_ticks_));

  FS_TRACE_INFO("Write Info:\n");
  FS_TRACE_INFO("  Wrote %zu MB of data and %zu MB of merkle trees\n",
                data_bytes_written_ / mb, merkle_bytes_written_ / mb);
  if (Collecting()) {
    FS_TRACE_INFO("  Enqueued to journal in %zu ms, made merkle tree in %zu ms\n",
                  TicksToMs(total_write_enqueue_time_ticks_),
                  TicksToMs(total_merkle_generation_time_ticks_));
  }

  FS_TRACE_INFO("Read Info:\n");
  FS_TRACE_INFO("  Opened %zu blobs (%zu MB)\n", blobs_opened_, blobs_opened_total_size_ / mb);

  auto verify_snapshot = verification_metrics_.Get();
  auto read_snapshot = read_metrics_.GetDiskRead();
  FS_TRACE_INFO("  Verified %zu blobs (%zu MB data, %zu MB merkle)\n",
                verify_snapshot.blobs_verified, verify_snapshot.data_size / mb,
                verify_snapshot.merkle_size / mb);
  FS_TRACE_INFO("  Read %zu MB from disk\n", read_snapshot.read_size / mb);
  if (Collecting()) {
    FS_TRACE_INFO("  Spent %zu ms reading, %zu ms verifying\n",
                  TicksToMs(zx::ticks(read_snapshot.read_time)),
                  TicksToMs(zx::ticks(verify_snapshot.verification_time)));
  }
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
  blobs_created_++;
  blobs_created_total_size_ += size_data;
  total_allocation_time_ticks_ += duration;
}

void BlobfsMetrics::UpdateLookup(uint64_t size) {
  blobs_opened_++;
  blobs_opened_total_size_ += size;
}

void BlobfsMetrics::UpdateClientWrite(uint64_t data_size, uint64_t merkle_size,
                                      const fs::Duration& enqueue_duration,
                                      const fs::Duration& generate_duration) {
  data_bytes_written_ += data_size;
  merkle_bytes_written_ += merkle_size;
  total_write_enqueue_time_ticks_ += enqueue_duration;
  total_merkle_generation_time_ticks_ += generate_duration;
}

void BlobfsMetrics::IncrementCompressionFormatMetric(const Inode& inode) {
  if (!Collecting()) {
    return;
  }
  fs_metrics::CompressionFormat format = FormatForInode(inode);
  cobalt_metrics_.mutable_compression_format_metrics()->IncrementCounter(format, inode.blob_size);
}

}  // namespace blobfs
