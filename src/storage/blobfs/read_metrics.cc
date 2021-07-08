// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/read_metrics.h"

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

ReadMetrics::PerCompressionMetrics* ReadMetrics::GetMetrics(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return &uncompressed_metrics_;
    case CompressionAlgorithm::kChunked:
      return &chunked_metrics_;
  }

  return nullptr;
}

ReadMetrics::PerCompressionInspect* ReadMetrics::GetInspect(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return &uncompressed_inspect_;
    case CompressionAlgorithm::kChunked:
      return &chunked_inspect_;
  }

  return nullptr;
}

void ReadMetrics::IncrementDiskRead(CompressionAlgorithm algorithm, uint64_t read_size,
                                    fs::Duration read_duration) {
  if (auto inspect = GetInspect(algorithm)) {
    inspect->read_ticks_node.Add(read_duration.get());
    inspect->read_bytes_node.Add(read_size);
  }
  // Hold the lock until metrics goes out of scope.
  std::lock_guard lock(lock_);
  if (auto metrics = GetMetrics(algorithm)) {
    metrics->read_ticks += read_duration;
    metrics->read_bytes += read_size;
  }
}

void ReadMetrics::IncrementDecompression(CompressionAlgorithm algorithm, uint64_t decompressed_size,
                                         fs::Duration decompress_duration, bool remote) {
  if (auto inspect = GetInspect(algorithm)) {
    inspect->decompress_ticks_node.Add(decompress_duration.get());
    inspect->decompress_bytes_node.Add(decompressed_size);
    if (remote) {
      remote_decompressions_node_.Add(1);
    }
  }
  // Hold the lock until metrics goes out of scope.
  std::lock_guard lock(lock_);
  if (auto metrics = GetMetrics(algorithm)) {
    metrics->decompress_ticks += decompress_duration;
    metrics->decompress_bytes += decompressed_size;
    if (remote) {
      remote_decompressions_++;
    }
  }
}

ReadMetrics::PerCompressionSnapshot ReadMetrics::GetSnapshot(CompressionAlgorithm algorithm) {
  // Hold the lock until metrics goes out of scope.
  std::lock_guard lock(lock_);
  if (auto metrics = GetMetrics(algorithm)) {
    return PerCompressionSnapshot{
        .read_ticks = metrics->read_ticks.get(),
        .read_bytes = metrics->read_bytes,
        .decompress_ticks = metrics->decompress_ticks.get(),
        .decompress_bytes = metrics->decompress_bytes,
    };
  }
  return PerCompressionSnapshot();
}

uint64_t ReadMetrics::GetRemoteDecompressions() const {
  std::lock_guard lock(lock_);
  return remote_decompressions_;
}

}  // namespace blobfs
