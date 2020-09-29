// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <string>

#include <fbl/algorithm.h>
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

BlobfsMetrics::BlobfsMetrics(bool should_record_page_in)
    : should_record_page_in(should_record_page_in) {
  // Add a node that allows querying the size of the Inspect VMO at runtime
  root_.CreateLazyNode(
      "inspect_vmo_stats",
      [this] {
        inspect::InspectStats stats = inspector_.GetStats();
        inspect::Inspector insp;
        insp.GetRoot().CreateUint("current_size", stats.size, &insp);
        insp.GetRoot().CreateUint("maximum_size", stats.maximum_size, &insp);
        return fit::make_result_promise(fit::ok(std::move(insp)));
      },
      &inspector_);
}

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

  FS_TRACE_INFO("Inspect VMO:\n");
  FS_TRACE_INFO("  Maximum Size (bytes) = %zu\n", inspector_.GetStats().maximum_size);
  FS_TRACE_INFO("  Current Size (bytes) = %zu\n", inspector_.GetStats().size);
  FS_TRACE_INFO("Page-in Metrics Recording Enabled = %s\n",
                should_record_page_in ? "true" : "false");
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

inspect::Inspector BlobfsMetrics::CreateInspector() {
  // The maximum size of the VMO is set to 128KiB. In practice, we have not seen this
  // inspect VMO need more than 128KiB. This gives the VMO enough space to grow if
  // we add more data in the future.
  // When recording page-in frequencies, a much larger Inspect VMO is required (>512KB).
  // TODO(fxbug.dev/59043): Inspect should print warnings about overflowing the maximum size of a
  // VMO.
#ifdef BLOBFS_ENABLE_LARGE_INSPECT_VMO
  constexpr size_t kSize = 2 * 1024 * 1024;
#else
  constexpr size_t kSize = 128 * 1024;
#endif
  return inspect::Inspector(inspect::InspectSettings{.maximum_size = kSize});
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

void BlobfsMetrics::IncrementPageIn(const fbl::String& merkle_hash, uint64_t offset,
                                    uint64_t length) {
  // Page-in metrics are a developer feature that is not intended to be used
  // in production. Enabling this feature also requires increasing the size of
  // the Inspect VMO considerably (>512KB).
  if (!should_record_page_in) {
    return;
  }

  inspect::InspectStats stats = inspector_.GetStats();
  if (stats.maximum_size <= stats.size) {
    FS_TRACE_ERROR("Blobfs has run out of space in the Inspect VMO.\n");
    FS_TRACE_ERROR("To record page-in metrics accurately, increase the VMO size.\n");
    FS_TRACE_ERROR("    Maximum size  : %zu\n", stats.maximum_size);
    FS_TRACE_ERROR("    Current size  : %zu\n", stats.size);
    should_record_page_in = false;
    return;
  }

  if (all_page_in_frequencies_.find(merkle_hash) == all_page_in_frequencies_.end()) {
    // We have no page in metrics on this blob yet. Create a new child node.
    all_page_in_frequencies_[merkle_hash].blob_root_node =
        page_in_frequency_stats_.CreateChild(merkle_hash.c_str());
  }

  BlobPageInFrequencies& blob_frequencies = all_page_in_frequencies_[merkle_hash];

  // Calculate the start+end frame indexes to increment
  uint32_t cur = fbl::round_down(offset, kBlobfsBlockSize) / kBlobfsBlockSize;
  uint32_t end = fbl::round_up(offset + length, kBlobfsBlockSize) / kBlobfsBlockSize;

  for (; cur < end; cur += 1) {
    if (blob_frequencies.offset_map.find(cur) == blob_frequencies.offset_map.end()) {
      // We have no frequencies recorded at this frame index. Create a new property.
      blob_frequencies.offset_map[cur] =
          blob_frequencies.blob_root_node.CreateUint(std::to_string(cur), 1);
    } else {
      blob_frequencies.offset_map[cur].Add(1);
    }
  }
}

}  // namespace blobfs
