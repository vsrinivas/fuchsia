// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_job_handle.h"
#include "src/developer/debug/debug_agent/mock_job_tree.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

class MockProcessStartHandler : public ProcessStartHandler {
 public:
  void OnProcessStart(const std::string& filter, std::unique_ptr<ProcessHandle> process) override {}
};

using ProcessHandleSetByKoid = DebuggedJob::ProcessHandleSetByKoid;

bool IsProcessMatched(const ProcessHandleSetByKoid& matches, const std::string& process_name) {
  for (const auto& match : matches) {
    if (match->GetName() == process_name)
      return true;
  }
  return false;
}

std::string PrintMatchedKoids(const ProcessHandleSetByKoid& matches) {
  std::stringstream ss;
  ss << "Matched koids: ";
  for (const auto& match : matches)
    ss << match->GetKoid() << " (" << match->GetName() << "), ";
  return ss.str();
}

}  // namespace

TEST(DebuggedJob, NoMatch) {
  MockProcessStartHandler start_handler;
  DebuggedJob job(&start_handler, GetMockJobTree());

  auto matches = job.SetFilters({"no-match"});
  EXPECT_TRUE(matches.empty());
}

TEST(DebuggedJob, SingleMatch) {
  MockProcessStartHandler start_handler;
  DebuggedJob job(&start_handler, GetMockJobTree());

  auto matches = job.SetFilters({"root-p1"});
  ASSERT_EQ(matches.size(), 1u) << PrintMatchedKoids(matches);
  EXPECT_TRUE(IsProcessMatched(matches, "root-p1"));
}

TEST(DebuggedJob, MultipleMatches) {
  MockProcessStartHandler start_handler;
  DebuggedJob job(&start_handler, GetMockJobTree());

  auto matches = job.SetFilters({"job121"});
  ASSERT_EQ(matches.size(), 2u) << PrintMatchedKoids(matches);
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p2"));
}

TEST(DebuggedJob, MultipleFilters) {
  MockProcessStartHandler start_handler;
  DebuggedJob job(&start_handler, GetMockJobTree());

  auto matches = job.SetFilters({"job11", "job12", "root"});

  ASSERT_EQ(matches.size(), 6u) << PrintMatchedKoids(matches);
  EXPECT_TRUE(IsProcessMatched(matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "root-p2"));
  EXPECT_TRUE(IsProcessMatched(matches, "root-p3"));
  EXPECT_TRUE(IsProcessMatched(matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p2"));
}

TEST(DebuggedJob, SubJobMatching) {
  MockProcessStartHandler start_handler;
  DebuggedJob job(&start_handler, GetMockJobTree());

  auto matches = job.SetFilters({"p1"});
  ASSERT_EQ(matches.size(), 4u) << PrintMatchedKoids(matches);
  EXPECT_TRUE(IsProcessMatched(matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p1"));

  // Find "job1" in the root.
  auto child_jobs = job.job_handle().GetChildJobs();
  auto found_job1 = std::find_if(child_jobs.begin(), child_jobs.end(),
                                 [](const auto& j) { return j->GetName() == "job1"; });
  ASSERT_NE(child_jobs.end(), found_job1);

  DebuggedJob job1(&start_handler, std::move(*found_job1));

  matches = job1.SetFilters({"p1"});
  ASSERT_EQ(matches.size(), 3u) << PrintMatchedKoids(matches);
  EXPECT_TRUE(IsProcessMatched(matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(matches, "job121-p1"));
}

}  // namespace debug_agent
