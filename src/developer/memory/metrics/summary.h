// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_
#define SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_

#include <zircon/types.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/developer/memory/metrics/capture.h"

namespace memory {

struct Sizes {
  Sizes() : private_bytes(0), scaled_bytes(0), total_bytes(0) {}
  Sizes(uint64_t b) : private_bytes(b), scaled_bytes(b), total_bytes(b) {}

  uint64_t private_bytes;
  uint64_t scaled_bytes;
  uint64_t total_bytes;
};

class ProcessSummary {
 public:
  static const zx_koid_t kKernelKoid;

  zx_koid_t koid() const { return koid_; }
  std::string name() const { return name_; }
  Sizes sizes() const { return sizes_; }
  const std::unordered_map<std::string, Sizes>& name_to_sizes() const { return name_to_sizes_; }
  const Sizes& GetSizes(std::string name) const;

 private:
  ProcessSummary(zx_koid_t koid, std::string name) : koid_(koid), name_(name) {}
  ProcessSummary(const zx_info_kmem_stats_t& kmem,
                 const std::unordered_map<zx_koid_t, const zx_info_vmo_t>& koid_to_vmo);

  zx_koid_t koid_;
  std::string name_;
  Sizes sizes_;
  std::unordered_set<zx_koid_t> vmos_;
  std::unordered_map<std::string, Sizes> name_to_sizes_;

  friend class Summary;
};

class Summary {
 public:
  Summary(const Capture& capture);

  void SortProcessSummaries();
  zx_time_t time() const { return time_; }
  const zx_info_kmem_stats_t& kstats() const { return kstats_; }
  const std::vector<ProcessSummary>& process_summaries() const { return process_summaries_; }

 private:
  zx_time_t time_;
  zx_info_kmem_stats_t kstats_;
  std::vector<ProcessSummary> process_summaries_;
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_
