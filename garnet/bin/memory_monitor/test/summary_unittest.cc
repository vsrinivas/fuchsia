// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/gtest/test_loop_fixture.h>

#include "garnet/bin/memory_monitor/capture.h"
#include "garnet/bin/memory_monitor/summary.h"
#include "garnet/bin/memory_monitor/test/test_utils.h"

namespace memory {
namespace test {

using SummaryUnitTest = gtest::TestLoopFixture;

TEST_F(SummaryUnitTest, Single) {
  // One process, one vmo.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1}},
    },
  });
  Summary s(c);
  ASSERT_EQ(1U, s.process_summaries().size());

  ProcessSummary ps = s.process_summaries().at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoVmos) {
  // One process, two vmos with same name.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1, 2}},
    },
  });
  Summary s(c);
  ASSERT_EQ(1U, s.process_summaries().size());

  ProcessSummary ps = s.process_summaries().at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoVmoNames) {
  // One process, two vmos with different names.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v2", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1, 2}}
    },
  });
  Summary s(c);
  ASSERT_EQ(1U, s.process_summaries().size());

  ProcessSummary ps = s.process_summaries().at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, Parent) {
  // One process, two vmos with different names, one is child.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {2}}
    },
  });
  Summary s(c);
  ASSERT_EQ(1U, s.process_summaries().size());

  ProcessSummary ps = s.process_summaries().at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcesses) {
  // Two processes, with different vmos.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v2", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1}},
      {.koid = 2, .name = "p2", .vmos = {2}},
    }
  });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  ProcessSummary ps = process_summaries.at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcessesShared) {
  // Two processes, with same vmos.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1}},
      {.koid = 2, .name = "p2", .vmos = {1}},
    }
  });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  ProcessSummary ps = process_summaries.at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcessesChild) {
  // Two processes, with one vmo shared through parantage.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {1}},
      {.koid = 2, .name = "p2", .vmos = {2}},
    }
  });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  ProcessSummary ps = process_summaries.at(0);
  EXPECT_EQ(1U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(150U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, MissingParent) {
  // Child VMO with parent koid that's not found.
  Capture c;
  TestUtils::CreateCapture(c, {
    .vmos = {
      {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
    },
    .processes = {
      {.koid = 1, .name = "p1", .vmos = {2}},
    }
  });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(1U, process_summaries.size());
  ProcessSummary ps = process_summaries.at(0);
  EXPECT_STREQ("p1", ps.name().c_str());
  EXPECT_EQ(1U, ps.koid());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

}  // namespace test
}  // namespace memory
