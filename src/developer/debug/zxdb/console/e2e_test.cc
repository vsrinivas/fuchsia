// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/main_e2e_test.h"

namespace zxdb {

class E2eTest : public TestWithLoop {
 public:
  E2eTest() {
    session_ = std::make_unique<Session>();
    mock_console_ = std::make_unique<MockConsole>(session_.get());
    mock_console_->ProcessInputLine(e2e_init_command);
  }

  ~E2eTest() { session_.reset(nullptr); }

  MockConsole& console() { return *mock_console_; }
  Session& session() { return *session_; }

 private:
  std::unique_ptr<Session> session_;
  std::unique_ptr<MockConsole> mock_console_;
};

TEST_F(E2eTest, CanConnect) {
  ASSERT_EQ("Connecting (use \"disconnect\" to cancel)...\n",
            console().GetOutputEvent().output.AsString());

  console().GetOutputEvent();
  EXPECT_TRUE(session().IsConnected());
}

}  // namespace zxdb
