// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <iterator>
#include <optional>
#include <utility>

#include <perftest/results.h>

namespace {

constexpr const char* kTestSuiteName = "fuchsia.kernel.boot";

// Each kcounter names a time point.  The corresponding "test result" names the
// interval between that time point and the previous one.
//
// **NOTE** Code in //zircon/kernel/top/handoff.cc and other places in the the
// kernel populate the "boot.timeline.*" kcounters with various time samples.
// This table is responsible for listing all of those sampling points in their
// intended chronological order and for giving appropriate names to each
// interval between two samples.  (The total interval from boot.timeline.zbi
// until boot.timeline.init is also published as "KernelBootTotal", below.)
// When new sample points are added in the kernel, new entries should be made
// here.  Take care in choosing the names for the intervals in this table, as
// these go into historical data collection under the "fuchsia.kernel.boot"
// test suite at https://chromeperf.appspot.com/report and changing these names
// can risk losing the correlation between historical data and new data.
constexpr std::pair<const char*, const char*> kTimelineSteps[] = {
    {"boot.timeline.zbi", "KernelBootLoader"},
    {"boot.timeline.physboot-setup", "KernelBootPhysSetup"},
    {"boot.timeline.decompress-start", "KernelBootPhysZbiScan"},
    {"boot.timeline.decompress-end", "KernelBootDecompression"},
    {"boot.timeline.physboot-handoff", "KernelBootPhysHandoff"},
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
  auto add_result = [ms_per_tick, &results](const char* result_name, zx_ticks_t before,
                                            zx_ticks_t after) {
    perftest::TestCaseResults* t = results.AddTestCase(kTestSuiteName, result_name, "milliseconds");
    t->AppendValue(static_cast<double>(after - before) * ms_per_tick);
  };

  // Export the difference in time between each stage of the timeline.
  std::optional<zx_ticks_t> first_step_ticks;
  zx_ticks_t last_step_ticks = 0;
  size_t missing_steps = 0;
  for (auto [name, result_name] : kTimelineSteps) {
    zx_ticks_t step_ticks = GetIntValueOrDie(node, name);
    if (!first_step_ticks) {
      first_step_ticks = step_ticks;
    } else if (step_ticks == 0) {
      // TODO(fxbug.dev/32414): With use_physboot=false the counters for the
      // intermediate steps will read as zero because those steps weren't done.
      // Omit those samples.
      ++missing_steps;
      continue;
    }
    add_result(result_name, last_step_ticks, step_ticks);
    last_step_ticks = step_ticks;
  }

  // Collect the soup-to-nuts interval from boot loader handoff to completion.
  ZX_ASSERT(first_step_ticks);
  ZX_ASSERT(last_step_ticks > *first_step_ticks);
  add_result("KernelBootTotal", *first_step_ticks, last_step_ticks);

  size_t expected_steps = std::size(kTimelineSteps) - missing_steps + 1;
  ZX_ASSERT_MSG(results.results()->size() == expected_steps,
                "found %zu results from %zu steps and %zu missing", results.results()->size(),
                std::size(kTimelineSteps), missing_steps);
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
