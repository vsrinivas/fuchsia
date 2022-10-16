// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/printer.h"

#include <lib/trace/event.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "lib/trace/internal/event_common.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/ostreamwrapper.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"
#include "third_party/rapidjson/include/rapidjson/writer.h"

namespace {

rapidjson::Document DocumentFromCapture(const memory::Capture& capture) {
  TRACE_DURATION("memory_metrics", "Printer::DocumentFromCapture");
  rapidjson::Document j(rapidjson::kObjectType);
  auto& a = j.GetAllocator();

  rapidjson::Value kernel(rapidjson::kObjectType);
  const auto& k = capture.kmem();
  kernel.AddMember("total", k.total_bytes, a)
      .AddMember("free", k.free_bytes, a)
      .AddMember("wired", k.wired_bytes, a)
      .AddMember("total_heap", k.total_heap_bytes, a)
      .AddMember("free_heap", k.free_heap_bytes, a)
      .AddMember("vmo", k.vmo_bytes, a)
      .AddMember("mmu", k.mmu_overhead_bytes, a)
      .AddMember("ipc", k.ipc_bytes, a)
      .AddMember("other", k.other_bytes, a);

  // Add additional kernel fields if kmem_extended is populated.
  const auto& k_ext = capture.kmem_extended();
  if (k_ext.total_bytes > 0) {
    kernel.AddMember("vmo_pager_total", k_ext.vmo_pager_total_bytes, a)
        .AddMember("vmo_pager_newest", k_ext.vmo_pager_newest_bytes, a)
        .AddMember("vmo_pager_oldest", k_ext.vmo_pager_oldest_bytes, a)
        .AddMember("vmo_discardable_locked", k_ext.vmo_discardable_locked_bytes, a)
        .AddMember("vmo_discardable_unlocked", k_ext.vmo_discardable_unlocked_bytes, a);
  }

  struct NameCount {
    std::string_view name_;
    mutable size_t count;
    explicit NameCount(const char* n) : name_(n, strlen(n)), count(1) {}

    bool operator==(const NameCount& kc) const { return name_ == kc.name_; }
  };

  class NameCountHash {
   public:
    size_t operator()(const NameCount& kc) const {
      return std::hash<std::string_view>()(kc.name_); }
  };

  TRACE_DURATION_BEGIN("memory_metrics", "Printer::DocumentFromCapture::Processes");
  std::unordered_set<NameCount, NameCountHash> name_count;
  rapidjson::Value processes(rapidjson::kArrayType);
  processes.Reserve(capture.koid_to_process().size(), a);
  rapidjson::Value process_header(rapidjson::kArrayType);
  processes.PushBack(process_header.PushBack("koid", a).PushBack("name", a).PushBack("vmos", a), a);
  for (const auto& [_, p] : capture.koid_to_process()) {
    rapidjson::Value vmos(rapidjson::kArrayType);
    vmos.Reserve(p.vmos.size(), a);
    for (const auto& v : p.vmos) {
      vmos.PushBack(v, a);
      auto [it, inserted] = name_count.emplace(capture.koid_to_vmo().find(v)->second.name);
      if (!inserted) {
        (*it).count++;
      }
    }
    rapidjson::Value process(rapidjson::kArrayType);
    process.PushBack(p.koid, a).PushBack(rapidjson::StringRef(p.name), a).PushBack(vmos, a);
    processes.PushBack(process, a);
  }
  TRACE_DURATION_END("memory_metrics", "Printer::DocumentFromCapture::Processes");

  TRACE_DURATION_BEGIN("memory_metrics", "Printer::DocumentFromCapture::Names");
  std::vector<NameCount> sorted_counts(name_count.begin(), name_count.end());
  std::sort(sorted_counts.begin(), sorted_counts.end(),
            [](const NameCount& kc1, const NameCount& kc2) { return kc1.count > kc2.count; });
  size_t index = 0;
  std::unordered_map<std::string_view, size_t> name_to_index(sorted_counts.size());
  for (const auto& kc : sorted_counts) {
    name_to_index[kc.name_] = index++;
  }

  rapidjson::Value vmo_names(rapidjson::kArrayType);
  for (const auto& nc : sorted_counts) {
    vmo_names.PushBack(rapidjson::StringRef(nc.name_.data()), a);
  }
  TRACE_DURATION_END("memory_metrics", "Printer::DocumentFromCapture::Names");

  TRACE_DURATION_BEGIN("memory_metrics", "Printer::DocumentFromCapture::Vmos");
  rapidjson::Value vmos(rapidjson::kArrayType);
  rapidjson::Value vmo_header(rapidjson::kArrayType);
  vmo_header.PushBack("koid", a)
      .PushBack("name", a)
      .PushBack("parent_koid", a)
      .PushBack("committed_bytes", a)
      .PushBack("allocated_bytes", a);
  vmos.PushBack(vmo_header, a);
  for (const auto& [k, v] : capture.koid_to_vmo()) {
    rapidjson::Value vmo_value(rapidjson::kArrayType);
    vmo_value.PushBack(v.koid, a)
        .PushBack(name_to_index[v.name], a)
        .PushBack(v.parent_koid, a)
        .PushBack(v.committed_bytes, a)
        .PushBack(v.allocated_bytes, a);
    vmos.PushBack(vmo_value, a);
  }
  TRACE_DURATION_END("memory_metrics", "Printer::DocumentFromCapture::Vmos");

  j.AddMember("Time", capture.time(), a)
      .AddMember("Kernel", kernel, a)
      .AddMember("Processes", processes, a)
      .AddMember("VmoNames", vmo_names, a)
      .AddMember("Vmos", vmos, a);

  return j;
}

}  // namespace

namespace memory {

const size_t kMaxFormattedStringSize = sizeof("1023.5T");

const char* FormatSize(uint64_t bytes, char* buf) {
  const char units[] = "BKMGTPE";
  uint16_t r = 0;
  int ui = 0;
  while (bytes > 1023) {
    r = bytes % 1024;
    bytes /= 1024;
    ui++;
  }
  unsigned int round_up = ((r % 102) >= 51);
  r = (r / 102) + round_up;
  if (r == 10) {
    bytes++;
    r = 0;
  }
  if (r == 0) {
    snprintf(buf, kMaxFormattedStringSize, "%zu%c", bytes, units[ui]);
  } else {
    snprintf(buf, kMaxFormattedStringSize, "%zu.%1u%c", bytes, r, units[ui]);
  }
  return buf;
}

void Printer::PrintCapture(const Capture& capture) {
  TRACE_DURATION("memory_metrics", "Printer::PrintCaptureJson");
  rapidjson::Document doc = DocumentFromCapture(capture);
  TRACE_DURATION_BEGIN("memory_metrics", "Printer::PrintCaptureJson::Write");
  rapidjson::OStreamWrapper osw(os_);
  rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
  doc.Accept(writer);
  TRACE_DURATION_END("memory_metrics", "Printer::PrintCaptureJson::Write");
}

void Printer::PrintCaptureAndBucketConfig(const Capture& capture,
                                          const std::string& bucket_config) {
  TRACE_DURATION("memory_metrics", "Printer::PrintCaptureAndBucketConfig");

  rapidjson::Document d(rapidjson::kObjectType);
  auto& a = d.GetAllocator();

  rapidjson::Document capture_doc = DocumentFromCapture(capture);
  d.AddMember("Capture", capture_doc, a);

  rapidjson::Document bucket_val;
  bucket_val.Parse(bucket_config.c_str());
  d.AddMember("Buckets", bucket_val, a);

  TRACE_DURATION_BEGIN("memory_metrics", "Printer::PrintCaptureAndBucketConfig::Write");
  rapidjson::OStreamWrapper osw(os_);
  rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
  d.Accept(writer);
  TRACE_DURATION_END("memory_metrics", "Printer::PrintCaptureAndBucketConfig::Write");
}

void Printer::OutputSizes(const Sizes& sizes) {
  if (sizes.total_bytes == sizes.private_bytes) {
    char private_buf[kMaxFormattedStringSize];
    os_ << FormatSize(sizes.private_bytes, private_buf) << "\n";
    return;
  }
  char private_buf[kMaxFormattedStringSize], scaled_buf[kMaxFormattedStringSize],
      total_buf[kMaxFormattedStringSize];
  os_ << FormatSize(sizes.private_bytes, private_buf) << " "
      << FormatSize(sizes.scaled_bytes, scaled_buf) << " "
      << FormatSize(sizes.total_bytes, total_buf) << "\n";
}

void Printer::PrintSummary(const Summary& summary, CaptureLevel level, Sorted sorted) {
  TRACE_DURATION("memory_metrics", "Printer::PrintSummary");
  char vmo_buf[kMaxFormattedStringSize], free_buf[kMaxFormattedStringSize];
  const auto& kstats = summary.kstats();
  os_ << "Time: " << summary.time() << " VMO: " << FormatSize(kstats.vmo_bytes, vmo_buf)
      << " Free: " << FormatSize(kstats.free_bytes, free_buf) << "\n";

  if (level == KMEM) {
    return;
  }

  const auto& summaries = summary.process_summaries();
  std::vector<uint32_t> summary_order;
  summary_order.reserve(summaries.size());
  for (uint32_t i = 0; i < summaries.size(); i++) {
    summary_order.push_back(i);
  }

  if (sorted == SORTED) {
    std::sort(summary_order.begin(), summary_order.end(), [&summaries](uint32_t ai, uint32_t bi) {
      const auto& a = summaries[ai];
      const auto& b = summaries[bi];
      return a.sizes().private_bytes > b.sizes().private_bytes;
    });
  }
  for (auto i : summary_order) {
    const auto& s = summaries[i];
    os_ << s.name() << "<" << s.koid() << "> ";
    OutputSizes(s.sizes());
    if (level == PROCESS) {
      continue;
    }

    const auto& name_to_sizes = s.name_to_sizes();
    std::vector<std::string> names;
    names.reserve(name_to_sizes.size());
    for (const auto& [name, sizes] : name_to_sizes) {
      names.push_back(name);
    }
    if (sorted == SORTED) {
      std::sort(names.begin(), names.end(),
                [&name_to_sizes](const std::string& a, const std::string& b) {
                  const auto& sa = name_to_sizes.at(a);
                  const auto& sb = name_to_sizes.at(b);
                  return sa.private_bytes == sb.private_bytes ? sa.scaled_bytes > sb.scaled_bytes
                                                              : sa.private_bytes > sb.private_bytes;
                });
    }
    for (const auto& name : names) {
      const auto& n_sizes = name_to_sizes.at(name);
      if (n_sizes.total_bytes == 0) {
        continue;
      }
      os_ << " " << name << " ";
      OutputSizes(n_sizes);
    }
  }
  os_ << std::flush;
}

void Printer::OutputSummary(const Summary& summary, Sorted sorted, zx_koid_t pid) {
  TRACE_DURATION("memory_metrics", "Printer::OutputSummary");
  const auto& summaries = summary.process_summaries();
  std::vector<ProcessSummary> sorted_summaries;
  if (sorted == SORTED) {
    sorted_summaries = summaries;
    std::sort(sorted_summaries.begin(), sorted_summaries.end(),
              [](ProcessSummary a, ProcessSummary b) {
                return a.sizes().private_bytes > b.sizes().private_bytes;
              });
  }
  const auto time = summary.time() / 1000000000;
  for (const auto& s : sorted == SORTED ? sorted_summaries : summaries) {
    if (pid != ZX_KOID_INVALID) {
      if (s.koid() != pid) {
        continue;
      }
      const auto& name_to_sizes = s.name_to_sizes();
      std::vector<std::string> names;
      names.reserve(name_to_sizes.size());
      for (const auto& [name, sizes] : name_to_sizes) {
        names.push_back(name);
      }
      if (sorted == SORTED) {
        std::sort(names.begin(), names.end(), [&name_to_sizes](std::string a, std::string b) {
          const auto& sa = name_to_sizes.at(a);
          const auto& sb = name_to_sizes.at(b);
          return sa.private_bytes == sb.private_bytes ? sa.scaled_bytes > sb.scaled_bytes
                                                      : sa.private_bytes > sb.private_bytes;
        });
      }
      for (const auto& name : names) {
        const auto& sizes = name_to_sizes.at(name);
        if (sizes.total_bytes == 0) {
          continue;
        }
        os_ << time << "," << s.koid() << "," << name << "," << sizes.private_bytes << ","
            << sizes.scaled_bytes << "," << sizes.total_bytes << "\n";
      }
      continue;
    }
    auto sizes = s.sizes();
    os_ << time << "," << s.koid() << "," << s.name() << "," << sizes.private_bytes << ","
        << sizes.scaled_bytes << "," << sizes.total_bytes << "\n";
  }
  os_ << std::flush;
}

void Printer::PrintDigest(const Digest& digest) {
  TRACE_DURATION("memory_metrics", "Printer::PrintDigest");
  for (auto const& bucket : digest.buckets()) {
    char size_buf[kMaxFormattedStringSize];
    FormatSize(bucket.size(), size_buf);
    os_ << bucket.name() << ": " << size_buf << "\n";
  }
}

void Printer::OutputDigest(const Digest& digest) {
  TRACE_DURATION("memory_metrics", "Printer::OutputDigest");
  auto const time = digest.time() / 1000000000;
  for (auto const& bucket : digest.buckets()) {
    os_ << time << "," << bucket.name() << "," << bucket.size() << "\n";
  }
}

}  // namespace memory
