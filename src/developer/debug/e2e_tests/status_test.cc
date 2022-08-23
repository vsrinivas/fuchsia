// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/remote_api.h"

namespace zxdb {

// The simplest test we can make where we connect and issue a single request for status from
// debug_agent. No processes should be attached or in limbo, and we should be able to gracefully
// disconnect.
TEST_F(E2eTest, CanConnect) {
  ASSERT_TRUE(session().IsConnected()) << "Not connected to DebugAgent.";
  Err status_err;
  debug_ipc::StatusReply reply;
  session().remote_api()->Status(
      {}, [this, reply, status_err](const Err& err, debug_ipc::StatusReply r) mutable {
        reply = std::move(r);
        status_err = err;
        this->loop().QuitNow();
      });

  loop().Run();

  EXPECT_TRUE(status_err.ok());
  EXPECT_EQ(reply.limbo.size(), 0ul);
  EXPECT_EQ(reply.processes.size(), 0ul);
}

}  // namespace zxdb
