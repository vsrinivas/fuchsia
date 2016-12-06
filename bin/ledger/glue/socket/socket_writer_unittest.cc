// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/socket/socket_writer.h"

#include <utility>

#include "apps/ledger/src/glue/socket/socket_drainer_client.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace glue {
namespace {

TEST(SocketWriter, WriteAndRead) {
  mtl::MessageLoop message_loop;
  glue::SocketPair socket;
  SocketWriter* writer = new SocketWriter();
  writer->Start("bazinga\n", std::move(socket.socket1));

  std::string value;
  auto drainer = std::make_unique<SocketDrainerClient>();
  drainer->Start(std::move(socket.socket2),
                 [&value, &message_loop](const std::string& v) {
                   value = v;
                   message_loop.PostQuitTask();
                 });
  message_loop.Run();

  EXPECT_EQ("bazinga\n", value);
}

TEST(SocketWriter, ClientClosedTheirEnd) {
  mtl::MessageLoop message_loop;
  glue::SocketPair socket;
  SocketWriter* writer = new SocketWriter();
  socket.socket2.reset();
  writer->Start("bazinga\n", std::move(socket.socket1));
}

}  // namespace
}  // namespace glue
