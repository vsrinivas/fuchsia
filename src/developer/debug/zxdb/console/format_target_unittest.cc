// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_target.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/mock_target.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

namespace {

TEST(FormatTarget, FormatTarget) {
  Session session;
  ConsoleContext context(&session);

  MockTarget target(&session);
  context.DidCreateTarget(&target);

  // There's a default target.
  EXPECT_EQ("Process 2 state=\"Not running\"\n", FormatTarget(&context, &target).AsString());

  MockProcess process(&target);
  target.SetRunningProcess(&process);

  EXPECT_EQ("Process 2 state=Running koid=0 name=\"Mock process\" component=component.cm\n",
            FormatTarget(&context, &target).AsString());
}

TEST(FormatTarget, DISABLED_FormatTargetList) {
  Session session;
  ConsoleContext context(&session);

  // There's a default target.
  EXPECT_EQ(
      "  # State       Koid Name Component\n"
      "▶ 1 Not running \n",
      FormatTargetList(&context, 0).AsString());

  MockTarget target(&session);
  // TODO(104366): This is a segmentation fault because CreateNewTarget expects a TargetImpl.
  session.system().CreateNewTarget(&target);
  MockProcess process(&target);
  target.SetRunningProcess(&process);

  EXPECT_EQ(
      "  # State       Koid Name         Component\n"
      "▶ 1 Not running \n"
      "  2 Running        0 Mock process component.cm\n",
      FormatTargetList(&context, 0).AsString());
}

}  // namespace

}  // namespace zxdb
