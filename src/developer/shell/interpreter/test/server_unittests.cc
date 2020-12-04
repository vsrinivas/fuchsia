// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/shell/interpreter/src/server.h"

TEST(Server, ShutdownService) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto server = std::make_unique<shell::interpreter::server::Server>(&loop);
  loop.StartThread();

  zx::channel client_end, server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
  ASSERT_EQ(ZX_OK, server->IncomingConnection(server_end.release()));

  // Call |fuchsia.shell/Shell.Shutdown| on the connection.
  llcpp::fuchsia::shell::Shell::SyncClient client(std::move(client_end));
  auto result = client.Shutdown();
  ASSERT_EQ(ZX_OK, result.status())
      << "Shutdown failed: " << result.status_string() << ", " << result.error();

  // We should observe the server proactively terminating the connection.
  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_OK,
            client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
  ASSERT_EQ(ZX_CHANNEL_PEER_CLOSED, observed);

  loop.Shutdown();
}

TEST(Server, ShutdownServer) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto server = std::make_unique<shell::interpreter::server::Server>(&loop);
  loop.StartThread();

  zx::channel client_end, server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
  ASSERT_EQ(ZX_OK, server->IncomingConnection(server_end.release()));

  // Shutdown the server first.
  async::PostTask(loop.dispatcher(), [server = std::move(server)] {});
  loop.Shutdown();

  // Verify that client connections are dropped.
  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_OK, client_end.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
  ASSERT_EQ(ZX_CHANNEL_PEER_CLOSED, observed);
}
