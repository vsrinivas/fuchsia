// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/status.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

TEST(Status, ConnectionStatus) {
  Session session;

  // No connection state.
  std::string no_conn_string = GetConnectionStatus(&session).AsString();
  EXPECT_NE(std::string::npos, no_conn_string.find("Not connected"));

  // Testing the connected connection status is currently difficult to mock and
  // is low-priority for testing. If Session were refactored this could become
  // practical.
}

TEST(Status, JobStatusNone) {
  Session empty_session;
  ConsoleContext empty_context(&empty_session);

  std::string no_conn_status = GetJobStatus(&empty_context).AsString();
  EXPECT_NE(std::string::npos, no_conn_status.find("Attached to 0 job(s)"));
}

}  // namespace zxdb
