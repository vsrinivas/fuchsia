// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_

#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/time.h>

#include <mutex>

#include <fs/ticker.h>

namespace blobfs {

// The |ReadMetrics| class tracks blobfs metrics that are updated on the read path, i.e. metrics
// related to disk reads and decompression.
//
// This class is thread-safe. The current blobfs implementation can update these metrics both from
// the blobfs main thread (for blobs that cannot be paged), and the userpager thread (for blobs that
// support paging).
class ReadMetrics {
 public:
  ReadMetrics() = default;
  ReadMetrics(const ReadMetrics&) = delete;
  ReadMetrics& operator=(const ReadMetrics&) = delete;

  // Increments aggregate information about reading blobs from storage
  // since mounting.
  void IncrementDiskRead(uint64_t read_size, fs::Duration read_duration);

  // Increments aggregate information about decompressing blobs from storage
  // since mounting.
  void IncrementDecompression(uint64_t compressed_size, uint64_t decompressed_size,
                              fs::Duration read_duration, fs::Duration decompress_duration);

  struct DiskReadSnapshot {
    uint64_t read_size;
    zx_ticks_t read_time;
  };

  struct DecompressionSnapshot {
    uint64_t compr_size;
    uint64_t decompr_size;
    zx_ticks_t compr_read_time;
    zx_ticks_t decompr_time;
  };

  // Returns a snapshot of the disk read metrics.
  DiskReadSnapshot GetDiskRead();

  // Returns a snapshot of the decompression metrics.
  DecompressionSnapshot GetDecompression();

 private:
  // Total time waiting for reads from disk.
  zx::ticks total_read_from_disk_time_ticks_ __TA_GUARDED(disk_read_mutex_) = {};
  uint64_t bytes_read_from_disk_ __TA_GUARDED(disk_read_mutex_) = 0;

  zx::ticks total_read_compressed_time_ticks_ __TA_GUARDED(decompr_mutex_) = {};
  zx::ticks total_decompress_time_ticks_ __TA_GUARDED(decompr_mutex_) = {};
  uint64_t bytes_compressed_read_from_disk_ __TA_GUARDED(decompr_mutex_) = 0;
  uint64_t bytes_decompressed_from_disk_ __TA_GUARDED(decompr_mutex_) = 0;

  std::mutex disk_read_mutex_;
  std::mutex decompr_mutex_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_READ_METRICS_H_
