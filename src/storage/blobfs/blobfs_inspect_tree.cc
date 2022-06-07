// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs_inspect_tree.h"

#include <atomic>

#include "src/storage/blobfs/blobfs.h"

namespace {

inspect::Inspector CreateInspector() {
#ifdef BLOBFS_ENABLE_LARGE_INSPECT_VMO
  // When recording page-in frequencies, a much larger Inspect VMO is required (>512KB).
  //
  // TODO(fxbug.dev/59043): Inspect should print warnings about overflowing the maximum size of a
  // VMO.
  const size_t kMaxInspectVmoSize = 2UL * 1024UL * 1024UL;
  return inspect::Inspector(inspect::InspectSettings{.maximum_size = kMaxInspectVmoSize});
#else
  // Use default inspect settings (currently sets the Inspect VMO size to 256KiB).
  return inspect::Inspector();
#endif
}

}  // namespace

namespace blobfs {

BlobfsInspectTree::BlobfsInspectTree()
    : inspector_(CreateInspector()),
      tree_root_(inspector_.GetRoot().CreateChild("blobfs")),
      opstats_node_(tree_root_.CreateChild("fs.opstats")),
      node_operations_(opstats_node_),
      detail_node_(tree_root_.CreateChild(fs_inspect::kDetailNodeName)),
      fragmentation_metrics_node_(detail_node_.CreateChild("fragmentation_metrics")),
      compression_metrics_node_(detail_node_.CreateChild("compression_metrics")),
      fs_inspect_nodes_(fs_inspect::CreateTree(tree_root_, CreateCallbacks())) {}

// Set general filesystem information.
void BlobfsInspectTree::SetInfo(const fs_inspect::InfoData& info) {
  std::lock_guard guard(info_mutex_);
  info_ = info;
}

// Update resource usage values that change when certain fields in the superblock are modified.
void BlobfsInspectTree::UpdateSuperblock(const Superblock& superblock) {
  std::lock_guard guard(usage_mutex_);
  usage_.total_bytes = superblock.data_block_count * superblock.block_size;
  usage_.used_bytes = superblock.alloc_block_count * superblock.block_size;
  usage_.total_nodes = superblock.inode_count;
  usage_.used_nodes = superblock.alloc_inode_count;
}

// Update FVM volume information and record any out of space events.
void BlobfsInspectTree::UpdateVolumeData(const block_client::BlockDevice& device,
                                         bool out_of_space) {
  zx::status<fs_inspect::VolumeData::SizeInfo> size_info =
      fs_inspect::VolumeData::GetSizeInfoFromDevice(device);
  if (size_info.is_error()) {
    FX_LOGS(WARNING) << "Failed to obtain size information from block device: "
                     << size_info.status_string();
  }
  std::lock_guard guard(volume_mutex_);
  if (size_info.is_ok()) {
    volume_.size_info = size_info.value();
  }
  if (out_of_space) {
    ++volume_.out_of_space_events;
  }
}

fs_inspect::NodeCallbacks BlobfsInspectTree::CreateCallbacks() {
  return {
      .info_callback =
          [this] {
            std::lock_guard guard(info_mutex_);
            return info_;
          },
      .usage_callback =
          [this] {
            std::lock_guard guard(usage_mutex_);
            return usage_;
          },
      .volume_callback =
          [this] {
            std::lock_guard guard(volume_mutex_);
            return volume_;
          },
  };
}

void BlobfsInspectTree::CalculateFragmentationMetrics(Blobfs& blobfs) {
  fragmentation_metrics_node_.AtomicUpdate([this, &blobfs](inspect::Node& node) {
    fragmentation_metrics_ = FragmentationMetrics(node);
    blobfs.CalculateFragmentationMetrics(fragmentation_metrics_);
  });
}

void BlobfsInspectTree::UpdateCompressionMetrics(const CompressionStats& stats) {
  compression_metrics_node_.AtomicUpdate([this, &stats](inspect::Node& node) {
    compression_metrics_ = CompressionStats::Metrics(node, stats);
  });
}

namespace {
inspect::ExponentialUintHistogram CreateFragmentationMetricsHistogram(std::string_view name,
                                                                      inspect::Node& root) {
  // These values must match the metric definitions in Cobalt.
  static constexpr uint64_t kFloor = 0;
  static constexpr uint64_t kInitialStep = 10;
  static constexpr uint64_t kStepMultiplier = 2;
  static constexpr size_t kBuckets = 10;
  return root.CreateExponentialUintHistogram(name, kFloor, kInitialStep, kStepMultiplier, kBuckets);
}
}  // namespace

FragmentationMetrics::FragmentationMetrics(inspect::Node& root)
    : total_nodes(root.CreateUint("total_nodes", 0u)),
      files_in_use(root.CreateUint("files_in_use", 0u)),
      extent_containers_in_use(root.CreateUint("extent_containers_in_use", 0u)),
      extents_per_file(CreateFragmentationMetricsHistogram("extents_per_file", root)),
      in_use_fragments(CreateFragmentationMetricsHistogram("in_use_fragments", root)),
      free_fragments(CreateFragmentationMetricsHistogram("free_fragments", root)) {}

void CompressionStats::Update(const InodePtr& inode) {
  static_assert(kBlobFlagMaskAnyCompression == kBlobFlagChunkCompressed,
                "Need to update compression stats to handle multiple formats.");
  if (inode->header.IsCompressedZstdChunked()) {
    zstd_chunked_bytes += inode->blob_size;
  } else {
    uncompressed_bytes += inode->blob_size;
  }
}

CompressionStats::Metrics::Metrics(inspect::Node& root, const CompressionStats& stats)
    : uncompressed_bytes(root.CreateUint("uncompressed_bytes", stats.uncompressed_bytes)),
      zstd_chunked_bytes(root.CreateUint("zstd_chunked_bytes", stats.zstd_chunked_bytes)) {}

}  // namespace blobfs
