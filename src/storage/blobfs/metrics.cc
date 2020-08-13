// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/zx/time.h>

#include <fs/metrics/events.h>
#include <fs/service.h>
#include <fs/trace.h>
#include <fs/vnode.h>

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

void PrintReadMetrics(ReadMetrics& metrics) {
  constexpr uint64_t mb = 1 << 20;
  auto snapshot = metrics.GetSnapshot(CompressionAlgorithm::UNCOMPRESSED);
  FS_TRACE_INFO("    Uncompressed: Read %zu MB (spent %zu ms)\n", snapshot.read_bytes / mb,
                TicksToMs(zx::ticks(snapshot.read_ticks)));

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::LZ4);
  FS_TRACE_INFO("    LZ4: Read %zu MB (spent %zu ms) | Decompressed %zu MB (spent %zu ms)\n",
                snapshot.read_bytes / mb, TicksToMs(zx::ticks(snapshot.read_ticks)),
                snapshot.decompress_bytes / mb, TicksToMs(zx::ticks(snapshot.decompress_ticks)));

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::CHUNKED);
  FS_TRACE_INFO("    Chunked: Read %zu MB (spent %zu ms) | Decompressed %zu MB (spent %zu ms)\n",
                snapshot.read_bytes / mb, TicksToMs(zx::ticks(snapshot.read_ticks)),
                snapshot.decompress_bytes / mb, TicksToMs(zx::ticks(snapshot.decompress_ticks)));

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::ZSTD);
  FS_TRACE_INFO("    ZSTD: Read %zu MB (spent %zu ms) | Decompressed %zu MB (spent %zu ms)\n",
                snapshot.read_bytes / mb, TicksToMs(zx::ticks(snapshot.read_ticks)),
                snapshot.decompress_bytes / mb, TicksToMs(zx::ticks(snapshot.decompress_ticks)));

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::ZSTD_SEEKABLE);
  FS_TRACE_INFO(
      "    ZSTD Seekable: Read %zu MB (spent %zu ms) | Decompressed %zu MB (spent %zu ms)\n",
      snapshot.read_bytes / mb, TicksToMs(zx::ticks(snapshot.read_ticks)),
      snapshot.decompress_bytes / mb, TicksToMs(zx::ticks(snapshot.decompress_ticks)));
}

void BlobfsMetrics::Dump() {
  constexpr uint64_t mb = 1 << 20;

  // Timings are only recorded when Cobalt metrics are enabled.

  FS_TRACE_INFO("Allocation Info:\n");
  FS_TRACE_INFO("  Allocated %zu blobs (%zu MB)\n", blobs_created_, blobs_created_total_size_ / mb);
  if (Collecting())
    FS_TRACE_INFO("  Total allocation time is %zu ms\n", TicksToMs(total_allocation_time_ticks_));

  FS_TRACE_INFO("Write Info:\n");
  FS_TRACE_INFO("  Wrote %zu MB of data and %zu MB of merkle trees\n", data_bytes_written_ / mb,
                merkle_bytes_written_ / mb);
  if (Collecting()) {
    FS_TRACE_INFO("  Enqueued to journal in %zu ms, made merkle tree in %zu ms\n",
                  TicksToMs(total_write_enqueue_time_ticks_),
                  TicksToMs(total_merkle_generation_time_ticks_));
  }

  FS_TRACE_INFO("Read Info:\n");
  FS_TRACE_INFO("  Paged:\n");
  PrintReadMetrics(paged_read_metrics_);
  FS_TRACE_INFO("  Unpaged:\n");
  PrintReadMetrics(unpaged_read_metrics_);

  FS_TRACE_INFO("  Merkle data read: %zu MB (spent %zu ms)\n", bytes_merkle_read_from_disk_ / mb,
                TicksToMs(zx::ticks(total_read_merkle_time_ticks_)));

  FS_TRACE_INFO("  Opened %zu blobs (%zu MB)\n", blobs_opened_, blobs_opened_total_size_ / mb);

  auto verify_snapshot = verification_metrics_.Get();
  FS_TRACE_INFO("  Verified %zu blobs (%zu MB data, %zu MB merkle)\n",
                verify_snapshot.blobs_verified, verify_snapshot.data_size / mb,
                verify_snapshot.merkle_size / mb);
  if (Collecting()) {
    FS_TRACE_INFO("  Spent %zu ms verifying\n",
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
  blobs_created_property_.Add(1);
  blobs_created_total_size_property_.Add(size_data);
  total_allocation_time_ticks_property_.Add(duration.get());
}

void BlobfsMetrics::UpdateLookup(uint64_t size) {
  blobs_opened_++;
  blobs_opened_total_size_ += size;
  blobs_opened_property_.Add(1);
  blobs_opened_total_size_property_.Add(size);
}

void BlobfsMetrics::UpdateClientWrite(uint64_t data_size, uint64_t merkle_size,
                                      const fs::Duration& enqueue_duration,
                                      const fs::Duration& generate_duration) {
  data_bytes_written_ += data_size;
  merkle_bytes_written_ += merkle_size;
  total_write_enqueue_time_ticks_ += enqueue_duration;
  total_merkle_generation_time_ticks_ += generate_duration;
  data_bytes_written_property_.Add(data_size);
  merkle_bytes_written_property_.Add(merkle_size);
  total_write_enqueue_time_ticks_property_.Add(enqueue_duration.get());
  total_merkle_generation_time_ticks_property_.Add(generate_duration.get());
}

void BlobfsMetrics::IncrementCompressionFormatMetric(const Inode& inode) {
  if (!Collecting()) {
    return;
  }
  fs_metrics::CompressionFormat format = FormatForInode(inode);
  cobalt_metrics_.mutable_compression_format_metrics()->IncrementCounter(format, inode.blob_size);
}

void BlobfsMetrics::IncrementMerkleDiskRead(uint64_t read_size, fs::Duration read_duration) {
  total_read_merkle_time_ticks_ += read_duration;
  bytes_merkle_read_from_disk_ += read_size;
}

}  // namespace blobfs
