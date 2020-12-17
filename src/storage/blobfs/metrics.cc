// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "src/storage/blobfs/metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <string>

#include <fbl/algorithm.h>
#include <fs/metrics/events.h>
#include <fs/service.h>
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
  FX_LOGS(INFO) << "    Uncompressed: Read " << snapshot.read_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.read_ticks)) << " ms)";

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::LZ4);
  FX_LOGS(INFO) << "    LZ4: Read " << snapshot.read_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.read_ticks)) << " ms) | Decompressed "
                << snapshot.decompress_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.decompress_ticks)) << " ms)";

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::CHUNKED);
  FX_LOGS(INFO) << "    Chunked: Read " << snapshot.read_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.read_ticks)) << " ms) | Decompressed "
                << snapshot.decompress_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.decompress_ticks)) << " ms)";

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::ZSTD);
  FX_LOGS(INFO) << "    ZSTD: Read " << snapshot.read_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.read_ticks)) << " ms) | Decompressed "
                << snapshot.decompress_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.decompress_ticks)) << " ms)";

  snapshot = metrics.GetSnapshot(CompressionAlgorithm::ZSTD_SEEKABLE);
  FX_LOGS(INFO) << "    ZSTD Seekable: Read " << snapshot.read_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.read_ticks)) << " ms) | Decompressed "
                << snapshot.decompress_bytes / mb << " MB (spent "
                << TicksToMs(zx::ticks(snapshot.decompress_ticks)) << " ms)";
  FX_LOGS(INFO) << "    Remote decompressions: " << metrics.remote_decompressions();
}

void BlobfsMetrics::Dump() {
  constexpr uint64_t mb = 1 << 20;

  // Timings are only recorded when Cobalt metrics are enabled.

  FX_LOGS(INFO) << "Allocation Info:";
  FX_LOGS(INFO) << "  Allocated " << blobs_created_ << " blobs (" << blobs_created_total_size_ / mb
                << " MB)";
  if (Collecting())
    FX_LOGS(INFO) << "  Total allocation time is " << TicksToMs(total_allocation_time_ticks_)
                  << " ms";

  FX_LOGS(INFO) << "Write Info:";
  FX_LOGS(INFO) << "  Wrote " << data_bytes_written_ / mb << " MB of data and "
                << merkle_bytes_written_ / mb << " MB of merkle trees";
  if (Collecting()) {
    FX_LOGS(INFO) << "  Enqueued to journal in " << TicksToMs(total_write_enqueue_time_ticks_)
                  << " ms, made merkle tree in " << TicksToMs(total_merkle_generation_time_ticks_)
                  << " ms";
  }

  FX_LOGS(INFO) << "Read Info:";
  FX_LOGS(INFO) << "  Paged:";
  PrintReadMetrics(paged_read_metrics_);
  FX_LOGS(INFO) << "  Unpaged:";
  PrintReadMetrics(unpaged_read_metrics_);

  FX_LOGS(INFO) << "  Merkle data read: " << bytes_merkle_read_from_disk_ / mb << " MB (spent "
                << TicksToMs(zx::ticks(total_read_merkle_time_ticks_)) << " ms)";

  FX_LOGS(INFO) << "  Opened " << blobs_opened_ << " blobs (" << blobs_opened_total_size_ / mb
                << " MB)";

  auto verify_snapshot = verification_metrics_.Get();
  FX_LOGS(INFO) << "  Verified " << verify_snapshot.blobs_verified << " blobs ("
                << verify_snapshot.data_size / mb << " MB data, "
                << verify_snapshot.merkle_size / mb << " MB merkle)";
  if (Collecting()) {
    FX_LOGS(INFO) << "  Spent " << TicksToMs(zx::ticks(verify_snapshot.verification_time))
                  << " ms verifying";
  }

  FX_LOGS(INFO) << "Inspect VMO:";
  FX_LOGS(INFO) << "  Maximum Size (bytes) = " << inspector_.GetStats().maximum_size;
  FX_LOGS(INFO) << "  Current Size (bytes) = " << inspector_.GetStats().size;
  FX_LOGS(INFO) << "Page-in Metrics Recording Enabled = "
                << (should_record_page_in ? "true" : "false");
}

void BlobfsMetrics::ScheduleMetricFlush() {
  async::PostDelayedTask(
      flush_loop_.dispatcher(),
      [this]() {
        cobalt_metrics_.Flush();
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
    FX_LOGS(ERROR) << "Blobfs has run out of space in the Inspect VMO.";
    FX_LOGS(ERROR) << "To record page-in metrics accurately, increase the VMO size.";
    FX_LOGS(ERROR) << "    Maximum size  : " << stats.maximum_size;
    FX_LOGS(ERROR) << "    Current size  : " << stats.size;
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
