// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/digest.h"

#include <regex>

#include <trace/event.h>

namespace memory {

const std::vector<const BucketMatch> Digest::kDefaultBucketMatches = {
    {"ZBI Buffer", "bin/bootsvc", ""},
    {"Graphics", ".*", "magma_create_buffer"},
    {"Video Buffer", "devhost:sys", "Sysmem.*"},
    {"Minfs", "minfs:/data", ".*"},
    {"Blobfs", "blobfs:/blob", ".*"},
    {"Opal", "io.flutter.product_runner.jit", ".*"},
    {"Web", "/pkg/web_engine_exe", ".*"},
    {"Kronk", "kronk.cmx", ".*"},
    {"Scenic", "scenic.cmx", ".*"},
    {"Amlogic", "devhost:pdev:05:00:f", ".*"},
    {"Netstack", "netstack.cmx", ".*"},
    {"Amber", "amber.cmx", ".*"},
    {"Pkgfs", "pkgfs", ".*"},
    {"Cast", "cast_agent.cmx", ".*"},
    {"Chromium", "chromium.cmx", ".*"},
};

Bucket::Bucket(const BucketMatch& match)
    : name_(match.name),
      process_(std::regex(match.process)),
      vmo_(std::regex(match.vmo)),
      size_(0) {}

Digest::Digest(const Capture& capture, const std::vector<const BucketMatch>& bucket_matches)
    : time_(capture.time()) {
  TRACE_DURATION("memory_metrics", "Digest::Digest");
  for (const auto& pair : capture.koid_to_vmo()) {
    undigested_vmos_.insert(pair.first);
  }
  for (const auto& bucket_match : bucket_matches) {
    buckets_.push_back(Bucket(bucket_match));
  }

  for (auto& bucket : buckets_) {
    for (const auto& pair : capture.koid_to_process()) {
      const auto& process = pair.second;

      if (!std::regex_match(process.name, bucket.process_)) {
        continue;
      }

      for (const auto& v : process.vmos) {
        if (undigested_vmos_.find(v) == undigested_vmos_.end()) {
          continue;
        }
        const auto& vmo = capture.vmo_for_koid(v);
        if (!std::regex_match(vmo.name, bucket.vmo_)) {
          continue;
        }
        bucket.vmos_.push_back(v);
        bucket.size_ += vmo.committed_bytes;
        undigested_vmos_.erase(v);
      }
    }
  }
  std::sort(buckets_.begin(), buckets_.end(),
            [](const Bucket& a, const Bucket& b) { return a.size() > b.size(); });
  if (undigested_vmos_.size() > 0) {
    uint64_t undigested_size = 0;
    for (auto v : undigested_vmos_) {
      undigested_size += capture.vmo_for_koid(v).committed_bytes;
    }
    buckets_.emplace_back("Undigested", undigested_size);
  }

  const auto& kmem = capture.kmem();
  if (kmem.total_bytes > 0) {
    uint64_t vmo_size = 0;
    for (const auto& bucket : buckets_) {
      vmo_size += bucket.size_;
    }
    if (vmo_size < kmem.vmo_bytes) {
      buckets_.emplace_back("Orphaned", kmem.vmo_bytes - vmo_size);
    }
    buckets_.emplace_back("Kernel", kmem.wired_bytes + kmem.total_heap_bytes +
                                        kmem.mmu_overhead_bytes + kmem.ipc_bytes +
                                        kmem.other_bytes);
    buckets_.emplace_back("Free", kmem.free_bytes);
  }
}

}  // namespace memory
