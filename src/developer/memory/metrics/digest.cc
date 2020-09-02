// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/digest.h"

#include <lib/trace/event.h>

#include <regex>

namespace memory {

bool BlobfsIsActiveVmo(const Vmo& vmo) {
  if (std::regex_match(vmo.name, std::regex("blob-[0-9a-f]+"))) {
    // Data VMOs are only active when they have clients
    return vmo.num_children > 0;
  }
  // Any other VMO is considered active
  return true;
}

// VMOs are bucketed into the first matching entry.
const std::vector<const BucketMatch> Digester::kDefaultBucketMatches = {
    {"ZBI Buffer", ".*", "uncompressed-bootfs"},
    // Memory used with the GPU or display hardware.
    {"Graphics", ".*", "magma_create_buffer|Mali .*|Magma.*|ImagePipe2Surface.*"},
    // Unused protected pool memory.
    {"ProtectedPool", "driver_host:sys", "SysmemAmlogicProtectedPool"},
    // Unused contiguous pool memory.
    {"ContiguousPool", "driver_host:sys", "SysmemContiguousPool"},
    {"Fshost", "fshost.cm", ".*"},
    {"Minfs", ".*minfs", ".*"},
    {"Blobfs", ".*blobfs", ".*", [](const Vmo& vmo) { return BlobfsIsActiveVmo(vmo); }},
    {"BlobfsInactive", ".*blobfs", ".*", [](const Vmo& vmo) { return !BlobfsIsActiveVmo(vmo); }},
    {"FlutterApps", "io\\.flutter\\..*", "dart.*"},
    {"Flutter", "io\\.flutter\\..*", ".*"},
    {"Web", "web_engine_exe:.*", ".*"},
    {"Kronk", "kronk.cmx|kronk_for_testing.cmx", ".*"},
    {"Scenic", "scenic.cmx", ".*"},
    {"Amlogic", "driver_host:pdev:05:00:f", ".*"},
    {"Netstack", "netstack.cmx", ".*"},
    {"Pkgfs", "pkgfs", ".*"},
    {"Cast", "cast_agent.cmx", ".*"},
    {"Archivist", "archivist.cm", ".*"},
    {"Cobalt", "cobalt.cmx", ".*"},
    {"Audio", "audio_core.cmx", ".*"},
    {"Context", "context_provider.cmx", ".*"},
};

BucketMatch::BucketMatch(const std::string& name, const std::string& process,
                         const std::string& vmo)
    : name_(name), process_(process), vmo_(vmo) {}

BucketMatch::BucketMatch(const std::string& name, const std::string& process,
                         const std::string& vmo, VmoMatcher vmo_matcher)
    : name_(name), process_(process), vmo_(vmo), vmo_matcher_(vmo_matcher) {}

bool BucketMatch::ProcessMatch(const std::string& process) {
  const auto& pi = process_match_.find(process);
  if (pi != process_match_.end()) {
    return pi->second;
  }
  bool match = std::regex_match(process, process_);
  process_match_.emplace(process, match);
  return match;
}

bool BucketMatch::VmoMatch(const Vmo& vmo) {
  const auto& vi = vmo_match_.find(vmo.name);
  if (vi != vmo_match_.end()) {
    return vi->second;
  }
  bool match = std::regex_match(vmo.name, vmo_);
  if (vmo_matcher_) {
    match &= (*vmo_matcher_)(vmo);
  }
  vmo_match_.emplace(vmo.name, match);
  return match;
}

Digest::Digest(const Capture& capture, Digester* digester) { digester->Digest(capture, this); }

Digester::Digester(const std::vector<const BucketMatch>& bucket_matches) {
  for (const auto& bucket_match : bucket_matches) {
    bucket_matches_.emplace_back(bucket_match);
  }
}

void Digester::Digest(const Capture& capture, class Digest* digest) {
  TRACE_DURATION("memory_metrics", "Digester::Digest");
  digest->time_ = capture.time();
  digest->undigested_vmos_.reserve(capture.koid_to_vmo().size());
  for (const auto& pair : capture.koid_to_vmo()) {
    digest->undigested_vmos_.emplace(pair.first);
  }

  digest->buckets_.reserve(bucket_matches_.size());
  for (auto& bucket_match : bucket_matches_) {
    auto& bucket = digest->buckets_.emplace_back(bucket_match.name(), 0);
    for (const auto& pair : capture.koid_to_process()) {
      const auto& process = pair.second;

      if (!bucket_match.ProcessMatch(process.name)) {
        continue;
      }
      for (const auto& v : process.vmos) {
        if (digest->undigested_vmos_.count(v) == 0) {
          continue;
        }
        const auto& vmo = capture.vmo_for_koid(v);
        if (!bucket_match.VmoMatch(vmo)) {
          continue;
        }
        bucket.size_ += vmo.committed_bytes;
        digest->undigested_vmos_.erase(v);
      }
    }
  }

  std::sort(digest->buckets_.begin(), digest->buckets_.end(),
            [](const Bucket& a, const Bucket& b) { return a.size() > b.size(); });
  uint64_t undigested_size = 0;
  for (auto v : digest->undigested_vmos_) {
    undigested_size += capture.vmo_for_koid(v).committed_bytes;
  }
  if (undigested_size > 0) {
    digest->buckets_.emplace_back("Undigested", undigested_size);
  }

  const auto& kmem = capture.kmem();
  if (kmem.total_bytes > 0) {
    uint64_t vmo_size = 0;
    for (const auto& bucket : digest->buckets_) {
      vmo_size += bucket.size_;
    }
    if (vmo_size < kmem.vmo_bytes) {
      digest->buckets_.emplace_back("Orphaned", kmem.vmo_bytes - vmo_size);
    }
    digest->buckets_.emplace_back("Kernel", kmem.wired_bytes + kmem.total_heap_bytes +
                                                kmem.mmu_overhead_bytes + kmem.ipc_bytes +
                                                kmem.other_bytes);
    digest->buckets_.emplace_back("Free", kmem.free_bytes);
  }
}

}  // namespace memory
