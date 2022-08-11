// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/filter.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_system_interface.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

namespace {

TEST(Filter, MatchProcess) {
  auto system_interface = MockSystemInterface::CreateWithData();

  //  j: 1 root
  //    j: 8 job1  /moniker  fuchsia-pkg://devhost/package#meta/component.cm
  //      j: 17 job12
  //        j: 18 job121
  //          p: 19 job121-p1
  auto process = system_interface->GetProcess(19);

  // Process name.
  debug_ipc::Filter filter{.type = debug_ipc::Filter::Type::kProcessName, .pattern = "job121-p1"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessName, .pattern = "p1"};
  EXPECT_FALSE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p1"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p2"};
  EXPECT_FALSE(Filter(filter).MatchesProcess(*process, *system_interface));

  // Job koid.
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p1", .job_koid = 1};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .job_koid = 8};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p1", .job_koid = 2};
  EXPECT_FALSE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kProcessName, .pattern = "p1", .job_koid = 1};
  EXPECT_FALSE(Filter(filter).MatchesProcess(*process, *system_interface));

  // Component info.
  filter = {.type = debug_ipc::Filter::Type::kComponentName, .pattern = "component.cm"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kComponentMoniker, .pattern = "/moniker"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
  filter = {.type = debug_ipc::Filter::Type::kComponentUrl,
            .pattern = "fuchsia-pkg://devhost/package#meta/component.cm"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(*process, *system_interface));
}

TEST(Filter, ApplyToJob) {
  auto system_interface = MockSystemInterface::CreateWithData();
  auto root = system_interface->GetRootJob();

  debug_ipc::Filter filter{.type = debug_ipc::Filter::Type::kProcessName, .pattern = "root-p1"};
  EXPECT_EQ(1ull, Filter(filter).ApplyToJob(*root, *system_interface).size());
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p1"};
  EXPECT_EQ(4ull, Filter(filter).ApplyToJob(*root, *system_interface).size());
  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "p1", .job_koid = 8};
  EXPECT_EQ(3ull, Filter(filter).ApplyToJob(*root, *system_interface).size());
  filter = {.type = debug_ipc::Filter::Type::kComponentName, .pattern = "component.cm"};
  EXPECT_EQ(5ull, Filter(filter).ApplyToJob(*root, *system_interface).size());
}

}  // namespace

}  // namespace debug_agent
