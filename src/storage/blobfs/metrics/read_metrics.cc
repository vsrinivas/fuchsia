// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/metrics/read_metrics.h"

#include <mutex>

#include "src/storage/blobfs/compression_settings.h"

namespace blobfs {

ReadMetrics::ReadMetrics(inspect::Node* read_metrics_node)
    : uncompressed_inspect_(read_metrics_node->CreateChild("uncompressed")),
      chunked_inspect_(read_metrics_node->CreateChild("chunked")),
      remote_decompressions_node_(
          read_metrics_node->CreateUint("remote_decompressions", remote_decompressions_)) {}

ReadMetrics::PerCompressionInspect::PerCompressionInspect(inspect::Node node)
    : parent_node(std::move(node)),
      read_ticks_node(parent_node.CreateInt("read_ticks", {})),
      read_bytes_node(parent_node.CreateUint("read_bytes", 0)),
      decompress_ticks_node(parent_node.CreateInt("decompress_ticks", {})),
      decompress_bytes_node(parent_node.CreateUint("decompress_bytes", 0)) {}

ReadMetrics::PerCompressionSnapshot ReadMetrics::GetSnapshot(CompressionAlgorithm algorithm) const {
  std::lock_guard lock(lock_);
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return uncompressed_metrics_;
    case CompressionAlgorithm::kChunked:
      return chunked_metrics_;
  }
}

ReadMetrics::PerCompressionSnapshot& ReadMetrics::MutableSnapshotLocked(
    CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return uncompressed_metrics_;
    case CompressionAlgorithm::kChunked:
      return chunked_metrics_;
  }
}

ReadMetrics::PerCompressionInspect& ReadMetrics::MutableInspect(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return uncompressed_inspect_;
    case CompressionAlgorithm::kChunked:
      return chunked_inspect_;
  }
}

void ReadMetrics::IncrementDiskRead(CompressionAlgorithm algorithm, uint64_t read_size,
                                    fs::Duration read_duration) {
  PerCompressionInspect& inspect = MutableInspect(algorithm);
  inspect.read_ticks_node.Add(read_duration.get());
  inspect.read_bytes_node.Add(read_size);

  // Hold the lock until snapshot goes out of scope.
  std::lock_guard lock(lock_);
  PerCompressionSnapshot& snapshot = MutableSnapshotLocked(algorithm);
  snapshot.read_ticks += read_duration.get();
  snapshot.read_bytes += read_size;
}

void ReadMetrics::IncrementDecompression(CompressionAlgorithm algorithm, uint64_t decompressed_size,
                                         fs::Duration decompress_duration, bool remote) {
  PerCompressionInspect& inspect = MutableInspect(algorithm);
  inspect.decompress_ticks_node.Add(decompress_duration.get());
  inspect.decompress_bytes_node.Add(decompressed_size);
  if (remote) {
    remote_decompressions_node_.Add(1);
  }

  // Hold the lock until snapshot goes out of scope.
  std::lock_guard lock(lock_);
  PerCompressionSnapshot& snapshot = MutableSnapshotLocked(algorithm);
  snapshot.decompress_ticks += decompress_duration.get();
  snapshot.decompress_bytes += decompressed_size;
  if (remote) {
    remote_decompressions_++;
  }
}

uint64_t ReadMetrics::GetRemoteDecompressions() const {
  std::lock_guard lock(lock_);
  return remote_decompressions_;
}

}  // namespace blobfs
