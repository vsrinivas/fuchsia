// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/memory_monitor/test/test_utils.h"

namespace memory {

// static.
void TestUtils::CreateCapture(memory::Capture& capture,
                              const CaptureTemplate& t) {
  capture.time_ = t.time;
  capture.kmem_= t.kmem;
  for (auto vmo : t.vmos) {
    capture.koid_to_vmo_.emplace(vmo.koid, vmo);
  }
  for (auto process : t.processes) {
    capture.koid_to_process_.emplace(process.koid, process);
  }
}

// static.
std::vector<ProcessSummary> TestUtils::GetProcessSummaries(
    const Summary& summary) {
  std::vector<ProcessSummary> summaries = summary.process_summaries();
  sort(summaries.begin(), summaries.end(),
       [](ProcessSummary a, ProcessSummary b) {
    return a.koid() < b.koid();
      });
  return summaries;
}
}  // namespace memory
