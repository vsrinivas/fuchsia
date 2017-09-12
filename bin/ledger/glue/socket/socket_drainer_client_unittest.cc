// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/socket/socket_drainer_client.h"

#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"

namespace glue {
namespace {

// Regression test for LE-229.
TEST(SocketDrainerClient, DoNotCallOnDelete) {
  fsl::MessageLoop message_loop;
  glue::SocketPair socket;
  socket.socket1.reset();

  auto drainer = std::make_unique<SocketDrainerClient>();
  bool called = false;
  drainer->set_on_empty([&called] { called = true; });
  drainer->Start(std::move(socket.socket2),
                 [&drainer, &message_loop](const std::string& v) {
                   drainer.reset();
                   message_loop.PostQuitTask();
                 });
  message_loop.Run();
  EXPECT_FALSE(called);
}

}  // namespace
}  // namespace glue
