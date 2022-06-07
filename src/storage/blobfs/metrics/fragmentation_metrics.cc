// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/metrics/fragmentation_metrics.h"

namespace {

// Helper function to create histograms matching the fragmentation metric definitions in Cobalt.
inspect::ExponentialUintHistogram CreateHistogram(std::string_view name, inspect::Node& node) {
  // These values must match the metric definitions in Cobalt.
  static constexpr uint64_t kFloor = 0;
  static constexpr uint64_t kInitialStep = 10;
  static constexpr uint64_t kStepMultiplier = 2;
  static constexpr size_t kBuckets = 10;
  return node.CreateExponentialUintHistogram(name, kFloor, kInitialStep, kStepMultiplier, kBuckets);
}

}  // namespace

namespace blobfs {

FragmentationMetrics::FragmentationMetrics(inspect::Node& node)
    : total_nodes(node.CreateUint("total_nodes", 0u)),
      files_in_use(node.CreateUint("files_in_use", 0u)),
      extent_containers_in_use(node.CreateUint("extent_containers_in_use", 0u)),
      extents_per_file(CreateHistogram("extents_per_file", node)),
      in_use_fragments(CreateHistogram("in_use_fragments", node)),
      free_fragments(CreateHistogram("free_fragments", node)) {}

}  // namespace blobfs
