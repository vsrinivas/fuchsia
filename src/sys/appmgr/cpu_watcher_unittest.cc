// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/cpu_watcher.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <algorithm>

#include <gtest/gtest.h>

namespace component {
namespace {

// CPU stats value injector for tests.

class FakeStatsReader final : public StatsReader {
 public:
  ~FakeStatsReader() override = default;
  // Values will be returned from the given vector, a new value on each fetch until
  // the last value is returned repeatedly. The vector must not be empty.
  // Each entry is <CPU time, queue time>.
  explicit FakeStatsReader(std::vector<zx_info_task_runtime_t> return_values)
      : return_values_(std::move(return_values)) {}

  // Takes a list of N integers. Returns a FakeStatsReader that will return N+1
  // readings (and then repeat the last one) where the first reading is 10,000 and
  // subsequent readings add the integer to the CPU sum. (queue is always 0.)
  // The first reading (10,000) will be read in AddTask() and discarded because the
  // elapsed time will be too short, so deltas[0] is the first number that will
  // show up in the histogram.
  static std::unique_ptr<FakeStatsReader> FromCpuDeltas(std::vector<int> deltas) {
    int sum = zx::nsec(10000).get();  // the first reading should be non-zero
    std::vector<zx_info_task_runtime_t> readings;
    readings.push_back(zx_info_task_runtime_t{.cpu_time = sum});
    for (int delta : deltas) {
      sum += delta;
      readings.push_back(zx_info_task_runtime_t{.cpu_time = sum});
    }
    return std::make_unique<FakeStatsReader>(readings);
  }

  // Returns the next / last pair<cpu_time, queue_time> from the fake value list.
  zx_status_t GetCpuStats(zx_info_task_runtime_t* info) override {
    if (next_return_ >= return_values_.size()) {
      next_return_--;
    }
    *info = return_values_[next_return_];
    next_return_++;
    return ZX_OK;
  }

 private:
  size_t next_return_ = 0;
  std::vector<zx_info_task_runtime_t> return_values_;
};

inspect::Hierarchy GetHierarchy(const inspect::Inspector& inspector) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  fpromise::result<inspect::Hierarchy> hierarchy;
  executor.schedule_task(inspect::ReadFromInspector(inspector).then(
      [&](fpromise::result<inspect::Hierarchy>& res) { hierarchy = std::move(res); }));
  while (!hierarchy) {
    loop.Run(zx::deadline_after(zx::sec(1)), true);
  }
  return hierarchy.take_value();
}

TEST(CpuWatcher, EmptyTasks) {
  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::nsec(1000),
                     },
                     nullptr /* stats_reader */);

  watcher.Measure();

  // Ensure that we do not record any measurements for an invalid job handle.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "measurements", "root"});
  ASSERT_NE(nullptr, node);
  EXPECT_TRUE(node->children().empty());
  EXPECT_TRUE(node->node().properties().empty());
}

int64_t PropertyIntValueOr(const inspect::Hierarchy* hierarchy, const std::string& name,
                           int64_t default_value) {
  const auto* prop = hierarchy->node().get_property<inspect::IntPropertyValue>(name);
  if (!prop) {
    return default_value;
  }
  return prop->value();
}

uint64_t PropertyUintValueOr(const inspect::Hierarchy* hierarchy, const std::string& name,
                             uint64_t default_value) {
  const auto* prop = hierarchy->node().get_property<inspect::UintPropertyValue>(name);
  if (!prop) {
    return default_value;
  }
  return prop->value();
}

TEST(CpuWatcher, BadTask) {
  zx::job self;
  zx_info_handle_basic_t basic;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic),
                                                    nullptr, nullptr));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(basic.rights & ~ZX_RIGHT_INSPECT, &self));

  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::nsec(1000),
                     },
                     nullptr /* stats_reader */);
  watcher.AddTask({"test_invalid"}, std::make_unique<JobStatsReader>(std::move(self)));
  watcher.Measure();

  // Ensure that we do not record any measurements for a task that cannot be read.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "measurements", "root", "test_invalid"});
  ASSERT_NE(nullptr, node);
  EXPECT_TRUE(node->children().empty());
  EXPECT_TRUE(node->node().properties().empty());
}

typedef std::vector<std::pair<int64_t, int64_t>> BucketPairs;

// Given and inspector and moniker, retrieves the CPU usage histogram.
// Returns a list of <bucket index, count> for buckets where count > 0.
// Returns <-1, -1> if no histogram is found.
BucketPairs GetHistogramNonZeroValues(const inspect::Inspector& inspector, std::string moniker) {
  BucketPairs not_found{std::pair(-1, -1)};
  const auto hierarchy = GetHierarchy(inspector);
  const auto* histogram_node = hierarchy.GetByPath({"test", "histograms"});
  if (histogram_node == nullptr) {
    return not_found;
  }
  const auto* histogram = histogram_node->node().get_property<inspect::UintArrayValue>(moniker);
  if (histogram == nullptr) {
    return not_found;
  }
  std::vector<std::pair<int64_t, int64_t>> output;
  for (const auto& bucket : histogram->GetBuckets()) {
    if (bucket.count > 0) {
      output.push_back(std::pair(std::max(bucket.floor, 0ul), bucket.count));
    }
  }
  return output;
}

// Test that the ceil function works: 0 cpu goes in bucket 0, 0.1..1 in bucket 1, etc.
TEST(CpuWatcher, BucketCutoffs) {
  zx::job self;
  inspect::Inspector inspector;
  zx::time time = zx::time(1000);
  // max_samples shouldn't have any effect on histograms; a small max_samples value is
  // supplied to verify that.
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .num_cpus = 1,
                         .sample_period = zx::nsec(1000),
                         .get_time = [&]() -> zx::time { return time; },
                     },
                     nullptr /* stats_reader */, 2 /* max_samples */);
  auto reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({1, 0, 500, 989, 990, 991, 999}));
  watcher.AddTask({"test", "valid", "12345"}, std::move(reader));

  time += zx::nsec(1000);
  watcher.Measure();  // 1
  auto answer = BucketPairs{std::pair(1, 1)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 0
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 500
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1), std::pair(50, 1)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 989
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1), std::pair(50, 1), std::pair(99, 1)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 990
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1), std::pair(50, 1), std::pair(99, 2)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 991
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1), std::pair(50, 1), std::pair(99, 2),
                       std::pair(100, 1)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 999
  answer = BucketPairs{std::pair(0, 1), std::pair(1, 1), std::pair(50, 1), std::pair(99, 2),
                       std::pair(100, 2)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);

  time += zx::nsec(1000);
  watcher.Measure();  // 0...
  answer = BucketPairs{std::pair(0, 2), std::pair(1, 1), std::pair(50, 1), std::pair(99, 2),
                       std::pair(100, 2)};
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), answer);
}

// Test that histograms are associated with their correct moniker. Two koids on the same
// moniker should share a histogram; distinct monikers should not.
TEST(CpuWatcher, MultiTaskHistograms) {
  zx::job self;
  inspect::Inspector inspector;
  zx::time time = zx::time(1000);
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .num_cpus = 1,
                         .sample_period = zx::nsec(1000),
                         .get_time = [&]() -> zx::time { return time; },
                     },
                     nullptr /* stats_reader */);
  auto task1_koid1_reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({110}));
  watcher.AddTask({"test", "valid1", "111"}, std::move(task1_koid1_reader));
  auto task1_koid2_reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({120}));
  watcher.AddTask({"test", "valid1", "222"}, std::move(task1_koid2_reader));
  auto task2_koid1_reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({210}));
  watcher.AddTask({"test", "valid2", "111"}, std::move(task2_koid1_reader));

  time += zx::nsec(1000);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid1"),
            (BucketPairs{std::pair(11, 1), std::pair(12, 1)}));
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid2"), BucketPairs{std::pair(21, 1)});
}

// Test that short time intervals (less than 90% of sample_period) are discarded
// both in watcher.Measure() and in watcher.removeTask(). Extra-long intervals
// should be recorded. In all cases, CPU % should be calculated over the actual
// interval, not the sample_period.
TEST(CpuWatcher, DiscardShortIntervals) {
  zx::job self;
  inspect::Inspector inspector;
  zx::time time = zx::time(1000);
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .num_cpus = 1,
                         .sample_period = zx::nsec(1000),
                         .get_time = [&]() -> zx::time { return time; },
                     },
                     nullptr /* stats_reader */);
  auto reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({100, 100, 100, 100}));
  watcher.AddTask({"test", "valid", "111"}, std::move(reader));

  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), BucketPairs{});

  time += zx::nsec(900);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), BucketPairs{std::pair(12, 1)});

  time += zx::nsec(899);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"),
            BucketPairs{std::pair(12, 1)});  // No change

  time += zx::nsec(2000);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"),
            (BucketPairs{std::pair(5, 1), std::pair(12, 1)}));

  time += zx::nsec(1000);
  watcher.RemoveTask({"test", "valid", "111"});
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"),
            (BucketPairs{std::pair(5, 1), std::pair(10, 1), std::pair(12, 1)}));

  auto reader2 = FakeStatsReader::FromCpuDeltas(std::vector<int>({100, 100, 100, 100}));
  watcher.AddTask({"test", "valid2", "111"}, std::move(reader2));

  time += zx::nsec(1000);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid2"), BucketPairs{std::pair(10, 1)});

  time += zx::nsec(899);
  watcher.RemoveTask({"test", "valid2", "111"});
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid2"),
            BucketPairs{std::pair(10, 1)});  // No change
}

// Test that the CPU% takes the number of cores into account - that is, with N cores
// the CPU% should be 1/N the amount it would be for 1 core.
TEST(CpuWatcher, DivideByCores) {
  zx::job self;
  inspect::Inspector inspector;
  zx::time time = zx::time(1000);
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .num_cpus = 4,
                         .sample_period = zx::nsec(1000),
                         .get_time = [&]() -> zx::time { return time; },
                     },
                     nullptr /* stats_reader */);
  auto reader = FakeStatsReader::FromCpuDeltas(std::vector<int>({400}));
  watcher.AddTask({"test", "valid", "111"}, std::move(reader));

  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), BucketPairs{});

  time += zx::nsec(1000);
  watcher.Measure();
  EXPECT_EQ(GetHistogramNonZeroValues(inspector, "test/valid"), BucketPairs{std::pair(10, 1)});
}

// Returns the number of valid samples under the given hierarchy, or -1 if any sample is invalid.
int64_t GetValidSampleCount(const inspect::Hierarchy* hierarchy) {
  if (!hierarchy) {
    printf("hierarchy is null!\n");
    return -1;
  }

  hierarchy = hierarchy->GetByPath({"@samples"});
  if (!hierarchy) {
    return 0;
  }

  size_t ret = 0;

  for (const auto& child : hierarchy->children()) {
    if (!std::all_of(child.name().begin(), child.name().end(),
                     [](char c) { return std::isdigit(c); })) {
      printf("name '%s' is not entirely numeric!\n", child.name().c_str());
      return -1;
    }

    auto check_int_nonzero = [&](const std::string& name) -> bool {
      const auto* prop = child.node().get_property<inspect::IntPropertyValue>(name);
      if (!prop) {
        printf("missing %s\n", name.c_str());
        return false;
      }
      if (prop->value() == 0) {
        printf("property %s is 0\n", name.c_str());
        return false;
      }
      return true;
    };

    if (!check_int_nonzero("timestamp") || !check_int_nonzero("cpu_time") ||
        !check_int_nonzero("queue_time")) {
      printf("invalid entry found at %s\n", child.name().c_str());
      return -1;
    }

    ret++;
  }

  return ret;
}

TEST(CpuWatcher, SampleSingle) {
  zx::job self;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));

  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::nsec(1000),
                     },
                     nullptr /* stats_reader */, 3 /* max_samples */);
  watcher.AddTask({"test_valid"}, std::make_unique<JobStatsReader>(std::move(self)));

  // Ensure that we record measurements up to the limit.
  auto hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      1, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      2, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      3, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  // One measurement rolled out.
  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      3, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  // Remove the task, the value is still there for now.
  watcher.RemoveTask({"test_valid"});
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      3, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  // Measurements roll out now.
  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      2, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(
      1, GetValidSampleCount(hierarchy.GetByPath({"test", "measurements", "root", "test_valid"})));

  // After the last measurement rolls out, the node is deleted.
  watcher.Measure();
  hierarchy = GetHierarchy(inspector);
  EXPECT_EQ(nullptr, hierarchy.GetByPath({"test", "measurements", "root", "test_valid"}));
}

TEST(CpuWatcher, SampleMultiple) {
  zx::job self;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));

  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::nsec(1000),
                     },
                     nullptr /* stats_reader */, 3 /* max_samples */);
  watcher.AddTask({"test_valid"}, std::make_unique<JobStatsReader>(std::move(self)));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  watcher.AddTask({"test_valid", "nested"}, std::make_unique<JobStatsReader>(std::move(self)));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  watcher.RemoveTask({"test_valid"});
  watcher.Measure();
  watcher.AddTask({"separate", "nested"}, std::make_unique<JobStatsReader>(std::move(self)));
  watcher.Measure();
  watcher.Measure();
  // Ensure total CPU rotates
  watcher.Measure();

  // Expected hierarchy:
  // root:
  //   test_valid: 0 samples
  //     nested: 3 samples
  //   separate: 0 samples
  //     nested: 3 samples

  auto hierarchy = GetHierarchy(inspector);

  inspect::Hierarchy *test_valid = nullptr, *test_valid_nested = nullptr,
                     *separate_nested = nullptr, *separate = nullptr;
  hierarchy.Visit([&](const std::vector<std::string>& path, inspect::Hierarchy* hierarchy) {
    if (path == std::vector<std::string>({"root", "test", "measurements", "root", "test_valid"})) {
      test_valid = hierarchy;
    } else if (path == std::vector<std::string>(
                           {"root", "test", "measurements", "root", "test_valid", "nested"})) {
      test_valid_nested = hierarchy;
    } else if (path ==
               std::vector<std::string>({"root", "test", "measurements", "root", "separate"})) {
      separate = hierarchy;
    } else if (path == std::vector<std::string>(
                           {"root", "test", "measurements", "root", "separate", "nested"})) {
      separate_nested = hierarchy;
    }
    return true;
  });

  EXPECT_EQ(0, GetValidSampleCount(test_valid));
  EXPECT_EQ(3, GetValidSampleCount(test_valid_nested));
  EXPECT_EQ(0, GetValidSampleCount(separate));
  EXPECT_EQ(3, GetValidSampleCount(separate_nested));

  // Check that total CPU contains the right number of measurements.
  auto* total = hierarchy.GetByPath({"test", "@total"});
  ASSERT_NE(nullptr, total);
  EXPECT_EQ(3u, total->children().size());
}

TEST(CpuWatcher, RecentCpu) {
  zx::job self;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::nsec(1000),
                     },
                     std::make_unique<JobStatsReader>(std::move(self)));

  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "recent_usage"});
  ASSERT_NE(nullptr, node);
  EXPECT_EQ(0u, PropertyIntValueOr(node, "recent_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "recent_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "recent_queue_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_queue_time", -1));

  watcher.Measure();

  hierarchy = GetHierarchy(inspector);
  node = hierarchy.GetByPath({"test", "recent_usage"});
  ASSERT_NE(nullptr, node);
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_timestamp", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_cpu_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_queue_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_queue_time", -1));

  watcher.Measure();

  hierarchy = GetHierarchy(inspector);
  node = hierarchy.GetByPath({"test", "recent_usage"});
  ASSERT_NE(nullptr, node);
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_timestamp", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_cpu_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_queue_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "previous_timestamp", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "previous_cpu_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "previous_queue_time", -1));
}

TEST(CpuWatcher, TotalCpuIncludesEndedJobs) {
  zx::job self;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::duration(1000),
                     },
                     nullptr /* stats_reader */);
  watcher.Measure();

  // This sample calculates 0 as the queue and CPU totals since there are no jobs.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "recent_usage"});
  ASSERT_NE(nullptr, node);
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "recent_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "recent_queue_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_queue_time", -1));

  watcher.AddTask({"testing"}, std::make_unique<JobStatsReader>(std::move(self)));
  watcher.RemoveTask({"testing"});
  watcher.Measure();

  // This sample collects the runtime from the exited job.
  hierarchy = GetHierarchy(inspector);
  node = hierarchy.GetByPath({"test", "recent_usage"});
  ASSERT_NE(nullptr, node);
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_timestamp", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_cpu_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "recent_queue_time", -1));
  EXPECT_LT(0u, PropertyIntValueOr(node, "previous_timestamp", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_cpu_time", -1));
  EXPECT_EQ(0u, PropertyIntValueOr(node, "previous_queue_time", -1));
}

// This test generates enough measurements to fill the output VMO.
// Note that it will need to be updated if the output size is increased or
// if future optimizations make Inspect space usage more efficient.
TEST(CpuWatcher, StressSize) {
  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"),
                     CpuWatcherParameters{
                         .sample_period = zx::duration(1000),
                     },
                     nullptr /* stats_reader */);

  // Make 1k tasks.
  for (size_t i = 0; i < 1000; i++) {
    zx::job self;
    ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
    watcher.AddTask({"test_entries", std::to_string(i)},
                    std::make_unique<JobStatsReader>(std::move(self)));
  }

  // Sample 60 times
  for (size_t i = 0; i < 60; i++) {
    watcher.Measure();
  }

  // Get the hierarchy and confirm it is out of measurement space.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "measurements", "@inspect"});
  ASSERT_NE(nullptr, node);
  EXPECT_NE(0u, PropertyUintValueOr(node, "maximum_size", 0));
  // Give a 100 byte margin of error on filling up the buffer.
  EXPECT_GT(PropertyUintValueOr(node, "current_size", 0),
            PropertyUintValueOr(node, "maximum_size", 0) - 100);
}

}  // namespace
}  // namespace component
