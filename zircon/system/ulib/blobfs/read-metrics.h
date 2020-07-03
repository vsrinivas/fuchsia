// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_

#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/time.h>

#include <mutex>

#include <blobfs/compression-settings.h>
#include <fs/ticker.h>

namespace blobfs {

// The |ReadMetrics| class tracks blobfs metrics that are updated on the read path, i.e. metrics
// related to disk reads and decompression.
//
// This class is thread-safe. Two instances of this class are stored in |BlobfsMetrics|,
// one for each thread.
class ReadMetrics {
 public:
  ReadMetrics() = default;
  ReadMetrics(const ReadMetrics&) = delete;
  ReadMetrics& operator=(const ReadMetrics&) = delete;

  // Increments aggregate information about reading blobs
  // from storage since mounting.
  void IncrementDiskRead(CompressionAlgorithm algorithm, uint64_t read_size,
                         fs::Duration read_duration);

  // Increments aggregate information about decompressing blobs from storage
  // since mounting.
  void IncrementDecompression(CompressionAlgorithm algorithm, uint64_t decompressed_size,
                              fs::Duration decompress_duration);

  struct PerCompressionSnapshot {
    // Metrics for reads from disk
    zx_ticks_t read_ticks;
    uint64_t read_bytes;

    // Metrics for decompression
    zx_ticks_t decompress_ticks;
    uint64_t decompress_bytes;
  };

  // Returns a snapshot of metrics recorded by this class.
  PerCompressionSnapshot GetSnapshot(CompressionAlgorithm algorithm);

 private:
  struct PerCompressionMetrics {
    // Metrics for reads from disk
    zx::ticks read_ticks = {};
    uint64_t read_bytes = 0;

    // Metrics for decompression
    zx::ticks decompress_ticks = {};
    uint64_t decompress_bytes = 0;
  };

  ReadMetrics::PerCompressionMetrics* GetMetrics(CompressionAlgorithm algorithm);

  PerCompressionMetrics uncompressed_metrics_ __TA_GUARDED(snapshot_mutex_) = {};
  PerCompressionMetrics lz4_metrics_ __TA_GUARDED(snapshot_mutex_) = {};
  PerCompressionMetrics zstd_metrics_ __TA_GUARDED(snapshot_mutex_) = {};
  PerCompressionMetrics chunked_metrics_ __TA_GUARDED(snapshot_mutex_) = {};

  // TODO(55545): This mutex is needed because when blobfs is being shutdown, the pager
  // thread is destroyed AFTER the main thread tries to dump metrics. That means it is
  // possible for the pager thread to be updating metrics while the data is being dumped
  // on the main thread. The bug linked is a refactor effort that will correct the
  // order of destruction so that this mutex will no longer be needed.
  std::mutex snapshot_mutex_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_
