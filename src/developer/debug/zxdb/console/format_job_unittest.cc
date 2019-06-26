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

class FormatJobTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MockRemoteAPI>();
    mock_remote_api_ = remote_api.get();
    return remote_api;
  }

  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

 private:
  MockRemoteAPI* mock_remote_api_ = nullptr;  // Owned by System.
};

}  // namespace

TEST_F(FormatJobTest, FormatJobContext) {
  // There should already be one default job context.
  session().system().CreateNewJobContext(nullptr);
  auto job_contexts = session().system().GetJobContexts();
  EXPECT_EQ(2u, job_contexts.size());

  ConsoleContext context(&session());

  // Empty job context (extra space at end is an implementation detail because
  // there's no name).
  EXPECT_EQ("Job 1 [Not attached] ", FormatJobContext(&context, job_contexts[0]).AsString());
}

}  // namespace zxdb
