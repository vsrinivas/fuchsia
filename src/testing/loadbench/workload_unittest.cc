// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "workload.h"

#include <lib/zircon-internal/ktrace.h>

#include <fstream>

#include <gtest/gtest.h>

namespace {
void MakeTestFile(const std::string filepath, const std::string contents) {
  std::ofstream file;
  file.open(filepath);
  file << contents;
  file.close();
}

TEST(WorkloadTest, ParseTracingTranslatesStringsToGroupMasksCorrectly) {
  const std::string test_json_filepath = "tmp/trace_config_parse_test.json";

  std::map<std::string, uint32_t> corresponding_string = {
      {"KTRACE_GRP_ALL", KTRACE_GRP_ALL},
      {"KTRACE_GRP_META", KTRACE_GRP_META},
      {"KTRACE_GRP_LIFECYCLE", KTRACE_GRP_LIFECYCLE},
      {"KTRACE_GRP_SCHEDULER", KTRACE_GRP_SCHEDULER},
      {"KTRACE_GRP_TASKS", KTRACE_GRP_TASKS},
      {"KTRACE_GRP_IPC", KTRACE_GRP_IPC},
      {"KTRACE_GRP_IRQ", KTRACE_GRP_IRQ},
      {"KTRACE_GRP_PROBE", KTRACE_GRP_PROBE},
      {"KTRACE_GRP_ARCH", KTRACE_GRP_ARCH},
      {"KTRACE_GRP_SYSCALL", KTRACE_GRP_SYSCALL},
      {"KTRACE_GRP_VM", KTRACE_GRP_VM},
      {"UNKNOWN", KTRACE_GRP_ALL},
  };

  for (auto& element : corresponding_string) {
    std::string contents = "{\"tracing\": {\"group mask\": \"" + element.first + "\",},}";
    uint32_t expected_group_mask = element.second;

    MakeTestFile(test_json_filepath, contents);
    Workload workload = Workload::Load("/tmp/trace_config_parse_test.json");

    ASSERT_TRUE(workload.tracing().has_value());
    ASSERT_EQ(expected_group_mask, workload.tracing().value().group_mask);
  }

  std::string contents = "{\"tracing\": {},}";
  uint32_t expected_group_mask = KTRACE_GRP_ALL;

  MakeTestFile(test_json_filepath, contents);
  Workload workload = Workload::Load("/tmp/trace_config_parse_test.json");

  ASSERT_TRUE(workload.tracing().has_value());
  ASSERT_EQ(expected_group_mask, workload.tracing().value().group_mask);
}

TEST(WorkloadTest, ParseTracingAssignsFilepath) {
  const std::string test_json_filepath = "tmp/trace_config_parse_test.json";
  const std::string human_readable_filepath = "/tmp/latest.ktrace";

  std::string contents = "{\"tracing\": {\"filepath\": \"" + human_readable_filepath + "\",},}";

  MakeTestFile(test_json_filepath, contents);
  Workload workload = Workload::Load("/tmp/trace_config_parse_test.json");

  ASSERT_TRUE(workload.tracing().has_value());
  ASSERT_TRUE(workload.tracing().value().filepath.has_value());
  ASSERT_EQ(human_readable_filepath, workload.tracing().value().filepath.value());
}

TEST(WorkloadTest, ParseTracingAssignsStringRef) {
  const std::string test_json_filepath = "tmp/trace_config_parse_test.json";
  const std::string string_ref = "test ref";

  std::string contents = "{\"tracing\": {\"string ref\": \"" + string_ref + "\",},}";

  MakeTestFile(test_json_filepath, contents);
  Workload workload = Workload::Load("/tmp/trace_config_parse_test.json");

  ASSERT_TRUE(workload.tracing().has_value());
  ASSERT_TRUE(workload.tracing().value().trace_string_ref.has_value());
  ASSERT_EQ(string_ref, workload.tracing().value().trace_string_ref.value());
}
}  // anonymous namespace
