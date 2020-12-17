// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_
#define SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_

#include <zircon/types.h>

#include <regex>
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
  ProcessSummary(zx_koid_t koid, const std::string& name) : koid_(koid), name_(name) {}
  ProcessSummary(const zx_info_kmem_stats_extended_t& kmem, uint64_t vmo_bytes);

  zx_koid_t koid() const { return koid_; }
  const std::string& name() const { return name_; }
  const Sizes& sizes() const { return sizes_; }
  const std::unordered_map<std::string, Sizes>& name_to_sizes() const { return name_to_sizes_; }
  const Sizes& GetSizes(std::string name) const;

 private:
  zx_koid_t koid_;
  std::string name_;
  Sizes sizes_;
  std::unordered_set<zx_koid_t> vmos_;
  std::unordered_map<std::string, Sizes> name_to_sizes_;

  friend class Summary;
};

struct NameMatch {
  const std::string regex;
  const std::string name;
};

class Namer {
 public:
  explicit Namer(const std::vector<const NameMatch>& name_matches);

  const std::string& NameForName(const std::string& name);

 private:
  struct RegexMatch {
    std::regex regex;
    std::string name;
  };

  std::vector<RegexMatch> regex_matches_;
  std::unordered_map<std::string, std::string> name_to_name_;
};

class Summary {
 public:
  Summary(const Capture& capture, Namer* namer);
  explicit Summary(const Capture& capture, const std::vector<const NameMatch>& name_matches =
                                               std::vector<const NameMatch>());
  Summary(const Capture& capture, Namer* namer,
          const std::unordered_set<zx_koid_t>& undigested_vmos);
  static const std::vector<const NameMatch> kNameMatches;

  void SortProcessSummaries();
  zx_time_t time() const { return time_; }
  const zx_info_kmem_stats_extended_t& kstats() const { return kstats_; }
  const std::vector<ProcessSummary>& process_summaries() const { return process_summaries_; }

 private:
  void Init(const Capture& capture, Namer* namer,
            const std::unordered_set<zx_koid_t>& undigested_vmos);
  zx_time_t time_;
  zx_info_kmem_stats_extended_t kstats_;
  std::vector<ProcessSummary> process_summaries_;
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_SUMMARY_H_
