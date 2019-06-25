// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/printer.h"

namespace memory {

std::string FormatSize(uint64_t bytes) {
  const char max_string[] = "1023.5T";
  const int max_string_size = sizeof(max_string);
  const char units[] = "BKMGTPE";
  char buf[max_string_size];
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
    snprintf(buf, max_string_size, "%zu%c", bytes, units[ui]);
  } else {
    snprintf(buf, max_string_size, "%zu.%1u%c", bytes, r, units[ui]);
  }
  return std::string(buf);
}

void Printer::PrintCapture(const Capture& capture, CaptureLevel level, Sorted sorted) {
  auto const& kmem = capture.kmem();
  os_ << "K," << capture.time() << "," << kmem.total_bytes << "," << kmem.free_bytes << ","
      << kmem.wired_bytes << "," << kmem.total_heap_bytes << "," << kmem.free_heap_bytes << ","
      << kmem.vmo_bytes << "," << kmem.mmu_overhead_bytes << "," << kmem.ipc_bytes << ","
      << kmem.other_bytes << "\n";
  if (level == KMEM) {
    return;
  }

  auto const& koid_to_process = capture.koid_to_process();
  std::vector<zx_koid_t> process_koids;
  for (auto const& pair : koid_to_process) {
    process_koids.push_back(pair.first);
  }
  if (sorted == SORTED) {
    sort(process_koids.begin(), process_koids.end(), [&koid_to_process](zx_koid_t a, zx_koid_t b) {
      auto const& sa = koid_to_process.at(a).stats;
      auto const& sb = koid_to_process.at(b).stats;
      return sa.mem_private_bytes == sb.mem_private_bytes
                 ? sa.mem_scaled_shared_bytes > sb.mem_scaled_shared_bytes
                 : sa.mem_private_bytes > sb.mem_private_bytes;
    });
  }
  for (auto const& koid : process_koids) {
    auto const& p = koid_to_process.at(koid);
    os_ << "P," << p.koid << "," << p.name << "," << p.stats.mem_mapped_bytes << ","
        << p.stats.mem_private_bytes << "," << p.stats.mem_shared_bytes << ","
        << p.stats.mem_scaled_shared_bytes;
    for (auto const& v : p.vmos) {
      os_ << "," << v;
    }
    os_ << "\n";
  }
  if (level == PROCESS) {
    return;
  }

  auto const& koid_to_vmo = capture.koid_to_vmo();
  std::vector<zx_koid_t> vmo_koids;
  for (auto const& pair : koid_to_vmo) {
    vmo_koids.push_back(pair.first);
  }
  if (sorted == SORTED) {
    sort(vmo_koids.begin(), vmo_koids.end(), [&koid_to_vmo](zx_koid_t a, zx_koid_t b) {
      auto const& sa = koid_to_vmo.at(a);
      auto const& sb = koid_to_vmo.at(b);
      return sa.committed_bytes > sb.committed_bytes;
    });
  }
  for (auto const& koid : vmo_koids) {
    auto const& v = koid_to_vmo.at(koid);
    os_ << "V," << v.koid << "," << v.name << "," << v.size_bytes << "," << v.parent_koid << ","
        << v.committed_bytes << "\n";
  }
  os_ << std::flush;
}

void Printer::PrintSummary(const Summary& summary, CaptureLevel level, Sorted sorted) {
  auto& kstats = summary.kstats();
  os_ << "Time: " << summary.time() << " VMO: " << FormatSize(kstats.vmo_bytes)
      << " Free: " << FormatSize(kstats.free_bytes) << "\n";

  if (level == KMEM) {
    return;
  }

  auto const& summaries = summary.process_summaries();
  std::vector<ProcessSummary> sorted_summaries;
  if (sorted == SORTED) {
    sorted_summaries = summaries;
    sort(sorted_summaries.begin(), sorted_summaries.end(), [](ProcessSummary a, ProcessSummary b) {
      return a.sizes().private_bytes > b.sizes().private_bytes;
    });
  }
  for (auto const& s : sorted == SORTED ? sorted_summaries : summaries) {
    os_ << s.name() << "<" << s.koid() << "> " << FormatSize(s.sizes().private_bytes);
    if (s.sizes().total_bytes == s.sizes().private_bytes) {
      os_ << "\n";
    } else {
      os_ << " " << FormatSize(s.sizes().scaled_bytes) << " " << FormatSize(s.sizes().total_bytes)
          << "\n";
    }
    if (level == PROCESS) {
      continue;
    }
    auto const& name_to_sizes = s.name_to_sizes();
    std::vector<std::string> names;
    for (auto const& pair : name_to_sizes) {
      names.push_back(pair.first);
    }
    if (sorted == SORTED) {
      sort(names.begin(), names.end(), [&name_to_sizes](std::string a, std::string b) {
        auto const& sa = name_to_sizes.at(a);
        auto const& sb = name_to_sizes.at(b);
        return sa.private_bytes == sb.private_bytes ? sa.scaled_bytes > sb.scaled_bytes
                                                    : sa.private_bytes > sb.private_bytes;
      });
    }
    for (auto const& name : names) {
      auto const& sizes = name_to_sizes.at(name);
      if (sizes.total_bytes == 0) {
        continue;
      }
      os_ << " " << name << " " << FormatSize(sizes.private_bytes);
      if (sizes.total_bytes == sizes.private_bytes) {
        os_ << "\n";
      } else {
        os_ << " " << FormatSize(sizes.scaled_bytes) << " " << FormatSize(sizes.total_bytes)
            << "\n";
      }
    }
  }
  os_ << std::flush;
}

void Printer::OutputSummary(const Summary& summary, Sorted sorted, zx_koid_t pid) {
  auto const& summaries = summary.process_summaries();
  std::vector<ProcessSummary> sorted_summaries;
  if (sorted == SORTED) {
    sorted_summaries = summaries;
    sort(sorted_summaries.begin(), sorted_summaries.end(), [](ProcessSummary a, ProcessSummary b) {
      return a.sizes().private_bytes > b.sizes().private_bytes;
    });
  }
  auto const time = summary.time() / 1000000000;
  for (auto const& s : sorted == SORTED ? sorted_summaries : summaries) {
    if (pid != ZX_KOID_INVALID) {
      if (s.koid() != pid) {
        continue;
      }
      auto const& name_to_sizes = s.name_to_sizes();
      std::vector<std::string> names;
      for (auto const& pair : name_to_sizes) {
        names.push_back(pair.first);
      }
      if (sorted == SORTED) {
        sort(names.begin(), names.end(), [&name_to_sizes](std::string a, std::string b) {
          auto const& sa = name_to_sizes.at(a);
          auto const& sb = name_to_sizes.at(b);
          return sa.private_bytes == sb.private_bytes ? sa.scaled_bytes > sb.scaled_bytes
                                                      : sa.private_bytes > sb.private_bytes;
        });
      }
      for (auto const& name : names) {
        auto const& sizes = name_to_sizes.at(name);
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

}  // namespace memory
