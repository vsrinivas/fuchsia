// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/status.h>

#include <iterator>
#include <utility>

#include <perftest/results.h>

#include "src/lib/inspect_deprecated/reader.h"

namespace {

constexpr std::pair<const char*, const char*> kSteps[] = {
    {"boot.timeline.zbi", "KernelBootLoader"},
    {"boot.timeline.virtual", "KernelBootPhysical"},
    {"boot.timeline.threading", "KernelBootThreads"},
    {"boot.timeline.userboot", "KernelBootUser"},
    {"boot.timeline.init", "KernelBootComplete"},
};

perftest::ResultsSet BootTimeline() {
  fuchsia::kernel::CounterSyncPtr kcounter;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(kcounter.NewRequest());

  fuchsia::mem::Buffer buffer;
  zx_status_t status;
  zx_status_t get_status = kcounter->GetInspectVmo(&status, &buffer);
  ZX_ASSERT_MSG(get_status == ZX_OK, "GetInspectVmo: %s", zx_status_get_string(get_status));
  ZX_ASSERT_MSG(status == ZX_OK, "GetInspectVmo yields status %s",
                zx_status_get_string(get_status));

  auto result = inspect_deprecated::ReadFromVmo(buffer.vmo);
  ZX_ASSERT_MSG(result.is_ok(), "ReadFromVmo failed");
  auto root = result.take_value();
  auto timeline = root.GetByPath({"boot", "timeline"});
  ZX_ASSERT_MSG(timeline, "boot.timeline not found");

  const auto& metrics = timeline->node().metrics();

  ZX_ASSERT(metrics.size() == std::size(kSteps));

  double ms_per_tick = 1000.0 / static_cast<double>(zx_ticks_per_second());

  perftest::ResultsSet results;
  uint64_t last_step_ticks = 0;
  for (auto [name, result_name] : kSteps) {
    auto it = std::find_if(metrics.begin(), metrics.end(),
                           [name = name](const auto& m) { return m.name() == name; });
    ZX_ASSERT_MSG(it != metrics.end(), "%s not found", name);

    uint64_t step_ticks = it->Get<inspect_deprecated::hierarchy::UIntMetric>().value();
    uint64_t elapsed = step_ticks - last_step_ticks;
    double elapsed_ms = static_cast<double>(elapsed) * ms_per_tick;
    last_step_ticks = step_ticks;

    auto* t = results.AddTestCase("fuchsia.kernel.boot", result_name, "milliseconds");
    t->AppendValue(elapsed_ms);
  }
  ZX_ASSERT(results.results()->size() == std::size(kSteps));

  return results;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s OUTFILE.json\n", argv[0]);
    return 1;
  }
  const char* outfile = argv[1];

  perftest::ResultsSet results = BootTimeline();

  return results.WriteJSONFile(outfile) ? 0 : 1;
}
