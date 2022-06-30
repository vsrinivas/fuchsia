// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/common/base_fidl_server.h"

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

namespace media_audio {
namespace {

// Arbitrarily selecting `GraphCreator` for this test since it's a small protocol.
class TestFidlServer : public BaseFidlServer<TestFidlServer, fuchsia_audio_mixer::GraphCreator> {
 public:
  static std::shared_ptr<TestFidlServer> CreateServer(
      async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end,
      int x, int y) {
    return BaseFidlServer::Create(dispatcher, std::move(server_end), x, y);
  }

  int x() const { return x_; }
  int y() const { return y_; }
  bool called_on_shutdown() const { return called_on_shutdown_; }

  // Create a child server.
  std::shared_ptr<TestFidlServer> CreateChildServer(
      async_dispatcher_t* dispatcher,
      fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end) {
    auto child = CreateServer(dispatcher, std::move(server_end), 0, 0);
    AddChildServer(child);
    return child;
  }

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::GraphCreator>.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override {}

 private:
  static inline constexpr std::string_view Name = "TestFidlServer";
  template <class ServerT, class ProtocolT>
  friend class ::media_audio::BaseFidlServer;

  TestFidlServer(int x, int y) : x_(x), y_(y) {}

  void OnShutdown(fidl::UnbindInfo info) override { called_on_shutdown_ = true; }

  const int x_;
  const int y_;
  bool called_on_shutdown_ = false;
};

TEST(BaseFidlServer, CreateAndShutdownOneServer) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  loop.StartThread("test-fidl-thread");

  auto endpoints = fidl::CreateEndpoints<fuchsia_audio_mixer::GraphCreator>().value();
  auto server =
      TestFidlServer::CreateServer(loop.dispatcher(), std::move(endpoints.server), 42, 99);

  EXPECT_EQ(server->x(), 42);
  EXPECT_EQ(server->y(), 99);
  server->Shutdown();
  EXPECT_TRUE(server->WaitForShutdown());
  EXPECT_TRUE(server->called_on_shutdown());
}

TEST(BaseFidlServer, CreateAndShutdownRecursive) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  loop.StartThread("test-fidl-thread");

  std::shared_ptr<TestFidlServer> root;
  std::shared_ptr<TestFidlServer> child;
  std::shared_ptr<TestFidlServer> grandchild;

  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_audio_mixer::GraphCreator>().value();
    root = TestFidlServer::CreateServer(loop.dispatcher(), std::move(endpoints.server), 42, 99);
  }
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_audio_mixer::GraphCreator>().value();
    child = root->CreateChildServer(loop.dispatcher(), std::move(endpoints.server));
  }
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_audio_mixer::GraphCreator>().value();
    grandchild = child->CreateChildServer(loop.dispatcher(), std::move(endpoints.server));
  }

  root->Shutdown();
  EXPECT_TRUE(root->WaitForShutdown());
  EXPECT_TRUE(root->called_on_shutdown());
  EXPECT_TRUE(child->called_on_shutdown());
  EXPECT_TRUE(grandchild->called_on_shutdown());
}

}  // namespace
}  // namespace media_audio
