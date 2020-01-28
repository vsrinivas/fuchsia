// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_job.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

namespace {

class FormatJobTest : public RemoteAPITest {};

}  // namespace

TEST_F(FormatJobTest, FormatJobContext) {
  // There should already be one default job context.
  session().system().CreateNewJobContext();
  auto job_contexts = session().system().GetJobContexts();
  EXPECT_EQ(2u, job_contexts.size());

  ConsoleContext context(&session());

  // Empty job context.
  EXPECT_EQ("Job 1 state=\"Not attached\" name=\"\"",
            FormatJobContext(&context, job_contexts[0]).AsString());
}

}  // namespace zxdb
