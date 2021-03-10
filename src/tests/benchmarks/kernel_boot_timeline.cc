// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/status.h>

#include <iterator>
#include <utility>

#include <perftest/results.h>

namespace {

constexpr const char* kTestSuiteName = "fuchsia.kernel.boot";

constexpr std::pair<const char*, const char*> kTimelineSteps[] = {
    {"boot.timeline.zbi", "KernelBootLoader"},
    {"boot.timeline.virtual", "KernelBootPhysical"},
    {"boot.timeline.threading", "KernelBootThreads"},
    {"boot.timeline.userboot", "KernelBootUser"},
    {"boot.timeline.init", "KernelBootComplete"},
};

// Return the property named `name` in the given node.
//
// Aborts if the name cannot be found or is of the wrong type.
int64_t GetIntValueOrDie(const inspect::NodeValue& node, const std::string& name) {
  const std::vector<inspect::PropertyValue>& properties = node.properties();

  auto it = std::find_if(properties.begin(), properties.end(),
                         [&name](const inspect::PropertyValue& m) { return m.name() == name; });
  ZX_ASSERT_MSG(it != properties.end(), "Key '%s' not found", name.c_str());
  ZX_ASSERT_MSG(it->Contains<inspect::IntPropertyValue>(),
                "Property '%s' was expected to be an IntMetric, but found format %d.", name.c_str(),
                static_cast<int>(it->format()));
  return it->Get<inspect::IntPropertyValue>().value();
}

void WriteBootTimelineStats(perftest::ResultsSet& results, const inspect::Hierarchy& timeline) {
  const inspect::NodeValue& node = timeline.node();
  ZX_ASSERT(node.properties().size() == std::size(kTimelineSteps));

  double ms_per_tick = 1000.0 / static_cast<double>(zx_ticks_per_second());

  // Export the difference in time between each stage of the timeline.
  int64_t last_step_ticks = 0;
  for (auto [name, result_name] : kTimelineSteps) {
    int64_t step_ticks = GetIntValueOrDie(node, name);
    int64_t elapsed = step_ticks - last_step_ticks;
    double elapsed_ms = static_cast<double>(elapsed) * ms_per_tick;
    last_step_ticks = step_ticks;

    auto* t = results.AddTestCase(kTestSuiteName, result_name, "milliseconds");
    t->AppendValue(elapsed_ms);
  }
  ZX_ASSERT(results.results()->size() == std::size(kTimelineSteps));
}

// Add a test result recording the amount of free memory after kernel init.
void WriteBootMemoryStats(perftest::ResultsSet& results, const inspect::Hierarchy& memory_stats) {
  int64_t value = GetIntValueOrDie(memory_stats.node(), "boot.memory.post_init_free_bytes");
  perftest::TestCaseResults* t =
      results.AddTestCase(kTestSuiteName, "KernelBootFreeMemoryAfterInit", "bytes");
  t->AppendValue(static_cast<double>(value));
}

perftest::ResultsSet GetBootStatistics() {
  fuchsia::kernel::CounterSyncPtr kcounter;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(kcounter.NewRequest());

  fuchsia::mem::Buffer buffer;
  zx_status_t status;
  zx_status_t get_status = kcounter->GetInspectVmo(&status, &buffer);
  ZX_ASSERT_MSG(get_status == ZX_OK, "GetInspectVmo: %s", zx_status_get_string(get_status));
  ZX_ASSERT_MSG(status == ZX_OK, "GetInspectVmo yields status %s",
                zx_status_get_string(get_status));

  auto result = inspect::ReadFromVmo(buffer.vmo);
  ZX_ASSERT_MSG(result.is_ok(), "ReadFromVmo failed");
  auto root = result.take_value();

  perftest::ResultsSet results;

  // Export boot timeline stats.
  const inspect::Hierarchy* timeline = root.GetByPath({"boot", "timeline"});
  ZX_ASSERT_MSG(timeline, "boot.timeline not found");
  WriteBootTimelineStats(results, *timeline);

  // Export boot memory stats.
  const inspect::Hierarchy* memory = root.GetByPath({"boot", "memory"});
  ZX_ASSERT_MSG(memory, "boot.memory not found");
  WriteBootMemoryStats(results, *memory);

  return results;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s OUTFILE.json\n", argv[0]);
    return 1;
  }
  const char* outfile = argv[1];

  perftest::ResultsSet results = GetBootStatistics();

  return results.WriteJSONFile(outfile) ? 0 : 1;
}
