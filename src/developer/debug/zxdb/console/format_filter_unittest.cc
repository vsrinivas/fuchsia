// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_filter.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

TEST(FormatFilter, FormatFilter) {
  Session session;
  ConsoleContext context(&session);

  Filter* f = session.system().CreateNewFilter();
  EXPECT_EQ("Filter 1 pattern=\"\" (disabled) job=* (all attached jobs)",
            FormatFilter(&context, f).AsString());

  f->SetPattern(Filter::kAllProcessesPattern);
  EXPECT_EQ("Filter 1 pattern=* (all processes) job=* (all attached jobs)",
            FormatFilter(&context, f).AsString());

  // This will be job 2 since the system should have a default job.
  Job* job = session.system().CreateNewJob();
  f->SetJob(job);
  f->SetPattern("foo");
  EXPECT_EQ("Filter 1 pattern=foo job=2", FormatFilter(&context, f).AsString());
}

TEST(FormatFilter, FormatFilterList) {
  Session session;
  ConsoleContext context(&session);

  EXPECT_EQ("No filters.\n", FormatFilterList(&context).AsString());
  EXPECT_EQ("    No filters.\n", FormatFilterList(&context, nullptr, 4).AsString());

  Filter* filter1 = session.system().CreateNewFilter();
  filter1->SetPattern("foo");
  session.system().CreateNewFilter();
  context.SetActiveFilter(filter1);

  EXPECT_EQ(
      "  # pattern job\n"
      "▶ 1 foo       *\n"
      "  2           *\n",
      FormatFilterList(&context).AsString());
}

}  // namespace zxdb
