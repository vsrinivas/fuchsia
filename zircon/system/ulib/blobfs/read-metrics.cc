// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "read-metrics.h"

#include <blobfs/compression-settings.h>

namespace blobfs {

ReadMetrics::ReadMetrics(inspect::Node* read_metrics_node)
    : uncompressed_metrics_(read_metrics_node->CreateChild("uncompressed")),
      lz4_metrics_(read_metrics_node->CreateChild("lz4")),
      zstd_metrics_(read_metrics_node->CreateChild("zstd")),
      zstd_seekable_metrics_(read_metrics_node->CreateChild("zstd_seekable")),
      chunked_metrics_(read_metrics_node->CreateChild("chunked")) {}

ReadMetrics::PerCompressionMetrics::PerCompressionMetrics(inspect::Node node):
    parent_node(std::move(node)),
    read_ticks_node(parent_node.CreateInt("read_ticks", read_ticks.get())),
    read_bytes_node(parent_node.CreateUint("read_bytes", read_bytes)),
    decompress_ticks_node(parent_node.CreateInt("decompress_ticks", decompress_ticks.get())),
    decompress_bytes_node(parent_node.CreateUint("decompress_bytes", decompress_bytes)) {}

ReadMetrics::PerCompressionMetrics* ReadMetrics::GetMetrics(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return &uncompressed_metrics_;
    case CompressionAlgorithm::LZ4:
      return &lz4_metrics_;
    case CompressionAlgorithm::ZSTD:
      return &zstd_metrics_;
    case CompressionAlgorithm::CHUNKED:
      return &chunked_metrics_;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return &zstd_seekable_metrics_;
  }
}

void ReadMetrics::IncrementDiskRead(CompressionAlgorithm algorithm, uint64_t read_size,
                                    fs::Duration read_duration) {
  auto metrics = GetMetrics(algorithm);
  metrics->read_ticks += read_duration;
  metrics->read_bytes += read_size;
  metrics->read_ticks_node.Add(read_duration.get());
  metrics->read_bytes_node.Add(read_size);
}

void ReadMetrics::IncrementDecompression(CompressionAlgorithm algorithm, uint64_t decompressed_size,
                                         fs::Duration decompress_duration) {
  auto metrics = GetMetrics(algorithm);
  metrics->decompress_ticks += decompress_duration;
  metrics->decompress_bytes += decompressed_size;
  metrics->decompress_ticks_node.Add(decompress_duration.get());
  metrics->decompress_bytes_node.Add(decompressed_size);
}

ReadMetrics::PerCompressionSnapshot ReadMetrics::GetSnapshot(CompressionAlgorithm algorithm) {
  auto metrics = GetMetrics(algorithm);
  return PerCompressionSnapshot{
      .read_ticks = metrics->read_ticks.get(),
      .read_bytes = metrics->read_bytes,
      .decompress_ticks = metrics->decompress_ticks.get(),
      .decompress_bytes = metrics->decompress_bytes,
  };
}

}  // namespace blobfs
