// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/cpu_watcher.h"

#include <lib/async-loop/default.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <gtest/gtest.h>

namespace component {
namespace {

inspect::Hierarchy GetHierarchy(const inspect::Inspector& inspector) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  fit::result<inspect::Hierarchy> hierarchy;
  executor.schedule_task(inspect::ReadFromInspector(inspector).then(
      [&](fit::result<inspect::Hierarchy>& res) { hierarchy = std::move(res); }));
  while (!hierarchy) {
    loop.Run(zx::deadline_after(zx::sec(1)), true);
  }
  return hierarchy.take_value();
}

TEST(CpuWatcher, EmptyTasks) {
  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"), zx::job());

  watcher.Measure();

  // Ensure that we do not record any measurements for an invalid job handle.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "measurements", "root"});
  ASSERT_NE(nullptr, node);
  EXPECT_TRUE(node->children().empty());
  EXPECT_TRUE(node->node().properties().empty());
}

TEST(CpuWatcher, BadTask) {
  zx::job self;
  zx_info_handle_basic_t basic;
  ASSERT_EQ(ZX_OK, zx::job::default_job()->get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic),
                                                    nullptr, nullptr));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(basic.rights & ~ZX_RIGHT_INSPECT, &self));

  inspect::Inspector inspector;
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"), zx::job());
  watcher.AddTask({"test_invalid"}, std::move(self));
  watcher.Measure();

  // Ensure that we do not record any measurements for a task that cannot be read.
  auto hierarchy = GetHierarchy(inspector);
  const auto* node = hierarchy.GetByPath({"test", "measurements", "root", "test_invalid"});
  ASSERT_NE(nullptr, node);
  EXPECT_TRUE(node->children().empty());
  EXPECT_TRUE(node->node().properties().empty());
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
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"), zx::job(), 3 /* max_samples */);
  watcher.AddTask({"test_valid"}, std::move(self));

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
  CpuWatcher watcher(inspector.GetRoot().CreateChild("test"), zx::job(), 3 /* max_samples */);
  watcher.AddTask({"test_valid"}, std::move(self));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  watcher.AddTask({"test_valid", "nested"}, std::move(self));
  ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self));
  watcher.RemoveTask({"test_valid"});
  watcher.Measure();
  watcher.AddTask({"separate", "nested"}, std::move(self));
  watcher.Measure();
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
}

}  // namespace
}  // namespace component
