// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_filter.h"

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

namespace {

class FormatFilterTest : public TestWithLoop {};

TEST_F(FormatFilterTest, FormatFilter) {
  Session session;
  ConsoleContext context(&session);

  Filter* f = session.system().CreateNewFilter();
  EXPECT_EQ("Filter 1 type=(unset) (invalid) ", FormatFilter(&context, f).AsString());

  f->SetType(debug_ipc::Filter::Type::kProcessNameSubstr);
  EXPECT_EQ("Filter 1 type=\"process name substr\" (invalid) ",
            FormatFilter(&context, f).AsString());

  f->SetType(debug_ipc::Filter::Type::kProcessName);
  f->SetPattern("foo");
  EXPECT_EQ("Filter 1 type=\"process name\" pattern=foo ", FormatFilter(&context, f).AsString());

  f->SetType(debug_ipc::Filter::Type::kProcessNameSubstr);
  f->SetPattern("");
  f->SetJobKoid(1234);
  EXPECT_EQ("Filter 1 type=\"process name substr\" job=1234 ",
            FormatFilter(&context, f).AsString());
}

TEST_F(FormatFilterTest, FormatFilterList) {
  Session session;
  ConsoleContext context(&session);

  EXPECT_EQ("No filters.\n", FormatFilterList(&context).AsString());
  EXPECT_EQ("    No filters.\n", FormatFilterList(&context, 4).AsString());

  Filter* filter1 = session.system().CreateNewFilter();
  filter1->SetType(debug_ipc::Filter::Type::kComponentName);
  filter1->SetPattern("foo.cm");
  session.system().CreateNewFilter();
  context.SetActiveFilter(filter1);

  EXPECT_EQ(
      "  # Type           Pattern Job          \n"
      "▶ 1 component name foo.cm               \n"
      "  2 (unset)                    (invalid)\n",
      FormatFilterList(&context).AsString());
}

}  // namespace

}  // namespace zxdb
