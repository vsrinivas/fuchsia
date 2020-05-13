// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace {

class MockProcessStartHandler : public ProcessStartHandler {
 public:
  void OnProcessStart(const std::string& filter, zx::process process) {}
};

bool IsProcessMatched(const MockObjectProvider& provider, const std::set<zx_koid_t>& matches,
                      const std::string& process_name) {
  auto process = provider.ProcessByName(process_name);
  FX_DCHECK(process) << "Could not find mock process " << process_name;
  return matches.find(process->koid) != matches.end();
}

std::string PrintMatchedKoids(const MockObjectProvider& provider,
                              const std::set<zx_koid_t>& matches) {
  std::stringstream ss;
  ss << "Matched koids: ";
  for (zx_koid_t koid : matches) {
    ss << koid;
    auto process = provider.ObjectByKoid(koid);
    if (process) {
      ss << " (" << process->name << ")";
    } else {
      ss << " (<NOT FOUND>)";
    }
    ss << ", ";
  }

  return ss.str();
}

// Tests -------------------------------------------------------------------------------------------

TEST(DebuggedJob, NoMatch) {
  std::shared_ptr<MockObjectProvider> provider = CreateDefaultMockObjectProvider();
  MockProcessStartHandler start_handler;
  const MockJobObject* root = provider->root();
  DebuggedJob job(provider, &start_handler, root->koid, zx::job(root->koid));

  auto matches = job.SetFilters({"no-match"});

  EXPECT_TRUE(matches.empty());
}

TEST(DebuggedJob, SingleMatch) {
  std::shared_ptr<MockObjectProvider> provider = CreateDefaultMockObjectProvider();
  MockProcessStartHandler start_handler;
  const MockJobObject* root = provider->root();
  DebuggedJob job(provider, &start_handler, root->koid, zx::job(root->koid));

  auto matches = job.SetFilters({"root-p1"});

  ASSERT_EQ(matches.size(), 1u) << PrintMatchedKoids(*provider, matches);
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "root-p1"));
}

TEST(DebuggedJob, MultipleMatches) {
  std::shared_ptr<MockObjectProvider> provider = CreateDefaultMockObjectProvider();
  MockProcessStartHandler start_handler;
  const MockJobObject* root = provider->root();
  DebuggedJob job(provider, &start_handler, root->koid, zx::job(root->koid));

  auto matches = job.SetFilters({"job121"});

  ASSERT_EQ(matches.size(), 2u) << PrintMatchedKoids(*provider, matches);
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p2"));
}

TEST(DebuggedJob, MultipleFilters) {
  std::shared_ptr<MockObjectProvider> provider = CreateDefaultMockObjectProvider();
  MockProcessStartHandler start_handler;
  const MockJobObject* root = provider->root();
  DebuggedJob job(provider, &start_handler, root->koid, zx::job(root->koid));

  auto matches = job.SetFilters({"job11", "job12", "root"});

  ASSERT_EQ(matches.size(), 6u) << PrintMatchedKoids(*provider, matches);
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "root-p2"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "root-p3"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p2"));
}

TEST(DebuggedJob, SubJobMatching) {
  std::shared_ptr<MockObjectProvider> provider = CreateDefaultMockObjectProvider();
  MockProcessStartHandler start_handler;
  const MockJobObject* root = provider->root();

  // Taking from the root.
  DebuggedJob job(provider, &start_handler, root->koid, zx::job(root->koid));
  auto matches = job.SetFilters({"p1"});

  ASSERT_EQ(matches.size(), 4u) << PrintMatchedKoids(*provider, matches);
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p1"));

  const MockObject* fake_sub_job = provider->JobByName("job1");
  ASSERT_TRUE(fake_sub_job);

  DebuggedJob sub_job(provider, &start_handler, fake_sub_job->koid, zx::job(fake_sub_job->koid));
  matches = sub_job.SetFilters({"p1"});

  ASSERT_EQ(matches.size(), 3u) << PrintMatchedKoids(*provider, matches);
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(*provider, matches, "job121-p1"));
}

}  // namespace
}  // namespace debug_agent
