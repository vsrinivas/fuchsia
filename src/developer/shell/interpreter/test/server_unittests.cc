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

  auto endpoints = fidl::CreateEndpoints<fuchsia_shell::Shell>();
  ASSERT_EQ(ZX_OK, server->IncomingConnection(std::move(endpoints->server)));

  // Call |fuchsia.shell/Shell.Shutdown| on the connection.
  fidl::WireSyncClient<fuchsia_shell::Shell> client(std::move(endpoints->client));
  auto result = client->Shutdown();
  ASSERT_EQ(ZX_OK, result.status()) << "Shutdown failed: " << result.error();

  // We should observe the server proactively terminating the connection.
  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_OK, client.client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                          zx::time::infinite(), &observed));
  ASSERT_EQ(ZX_CHANNEL_PEER_CLOSED, observed);

  loop.Shutdown();
}

TEST(Server, ShutdownServer) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto server = std::make_unique<shell::interpreter::server::Server>(&loop);
  loop.StartThread();

  auto endpoints = fidl::CreateEndpoints<fuchsia_shell::Shell>();
  ASSERT_EQ(ZX_OK, server->IncomingConnection(std::move(endpoints->server)));

  // Shutdown the server first.
  async::PostTask(loop.dispatcher(), [server = std::move(server)] {});
  loop.Shutdown();

  // Verify that client connections are dropped.
  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_OK, endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                        zx::time::infinite(), &observed));
  ASSERT_EQ(ZX_CHANNEL_PEER_CLOSED, observed);
}
