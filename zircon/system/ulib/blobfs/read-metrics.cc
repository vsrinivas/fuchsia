// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "read-metrics.h"

namespace blobfs {

void ReadMetrics::IncrementDiskRead(uint64_t read_size, fs::Duration read_duration) {
  std::scoped_lock guard(disk_read_mutex_);
  total_read_from_disk_time_ticks_ += read_duration;
  bytes_read_from_disk_ += read_size;
}

void ReadMetrics::IncrementDecompression(uint64_t compressed_size, uint64_t decompressed_size,
                                         fs::Duration read_duration,
                                         fs::Duration decompress_duration) {
  std::scoped_lock guard(decompr_mutex_);
  bytes_compressed_read_from_disk_ += compressed_size;
  bytes_decompressed_from_disk_ += decompressed_size;
  total_read_compressed_time_ticks_ += read_duration;
  total_decompress_time_ticks_ += decompress_duration;
}

ReadMetrics::DiskReadSnapshot ReadMetrics::GetDiskRead() {
  std::scoped_lock guard(disk_read_mutex_);
  return DiskReadSnapshot{
      .read_size = bytes_read_from_disk_,
      .read_time = total_read_from_disk_time_ticks_.get(),
  };
}

ReadMetrics::DecompressionSnapshot ReadMetrics::GetDecompression() {
  std::scoped_lock guard(decompr_mutex_);
  return DecompressionSnapshot{
      .compr_size = bytes_compressed_read_from_disk_,
      .decompr_size = bytes_decompressed_from_disk_,
      .compr_read_time = total_read_compressed_time_ticks_.get(),
      .decompr_time = total_decompress_time_ticks_.get(),
  };
}

}  // namespace blobfs
