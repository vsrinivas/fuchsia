// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "garnet/bin/memory_monitor/capture.h"
#include "garnet/bin/memory_monitor/summary.h"

namespace memory {

struct CaptureTemplate {
  zx_time_t time;
  zx_info_kmem_stats_t kmem;
  std::vector<zx_info_vmo_t> vmos;
  std::vector<Process> processes;
};

class TestUtils {
 public:
  static void CreateCapture(memory::Capture& capture, const CaptureTemplate& t);

  // Sorted by koid.
  static std::vector<ProcessSummary> GetProcessSummaries(
      const Summary& summary);
};

}  // namespace memory
