// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/printer.h"

#include <algorithm>
#include <cstdint>

#include <trace/event.h>

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

void Printer::PrintCapture(const Capture& capture, CaptureLevel level, Sorted sorted) {
  TRACE_DURATION("memory_metrics", "Printer::PrintCapture");
  const auto& kmem = capture.kmem();
  os_ << "K," << capture.time() << "," << kmem.total_bytes << "," << kmem.free_bytes << ","
      << kmem.wired_bytes << "," << kmem.total_heap_bytes << "," << kmem.free_heap_bytes << ","
      << kmem.vmo_bytes << "," << kmem.mmu_overhead_bytes << "," << kmem.ipc_bytes << ","
      << kmem.other_bytes << "\n";
  if (level == KMEM) {
    return;
  }

  const auto& koid_to_process = capture.koid_to_process();
  std::vector<zx_koid_t> process_koids;
  for (const auto& pair : koid_to_process) {
    process_koids.push_back(pair.first);
  }
  for (const auto& koid : process_koids) {
    const auto& p = koid_to_process.at(koid);
    os_ << "P," << p.koid << "," << p.name;
    for (const auto& v : p.vmos) {
      os_ << "," << v;
    }
    os_ << "\n";
  }

  const auto& koid_to_vmo = capture.koid_to_vmo();
  std::vector<zx_koid_t> vmo_koids;
  for (const auto& pair : koid_to_vmo) {
    vmo_koids.push_back(pair.first);
  }
  if (sorted == SORTED) {
    std::sort(vmo_koids.begin(), vmo_koids.end(), [&koid_to_vmo](zx_koid_t a, zx_koid_t b) {
      const auto& sa = koid_to_vmo.at(a);
      const auto& sb = koid_to_vmo.at(b);
      return sa.committed_bytes > sb.committed_bytes;
    });
  }
  for (const auto& koid : vmo_koids) {
    const auto& v = koid_to_vmo.at(koid);
    os_ << "V," << v.koid << "," << v.name << "," << v.parent_koid << "," << v.committed_bytes
        << "\n";
  }
  os_ << std::flush;
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
    for (const auto& pair : name_to_sizes) {
      names.push_back(pair.first);
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
      for (const auto& pair : name_to_sizes) {
        names.push_back(pair.first);
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
