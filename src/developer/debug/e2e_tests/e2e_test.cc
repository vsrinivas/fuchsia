// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/e2e_tests/e2e_test.h"

#include "src/developer/debug/e2e_tests/main_e2e_test.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

void E2eTest::SetUp() {
  session_ = std::make_unique<Session>();

  ASSERT_NE(bridge, nullptr) << "debug_agent bridge failed to initialize.";
  socket_path_ = bridge->GetDebugAgentSocketPath();

  Err e = ConnectToDebugAgent();
  EXPECT_TRUE(e.ok()) << e.msg();
}

void E2eTest::TearDown() {
  Err e = session().Disconnect();
  EXPECT_TRUE(e.ok()) << e.msg();

  session_.reset();
}

Err E2eTest::ConnectToDebugAgent() {
  SessionConnectionInfo info;
  info.type = SessionConnectionType::kUnix;
  info.host = socket_path_;

  Err err;

  session_->Connect(info, [&err, this](const Err& e) {
    err = e;
    loop().QuitNow();
  });

  loop().Run();

  return err;
}

}  // namespace zxdb
