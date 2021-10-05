// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.simple/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

namespace {

class Server : public fidl::WireServer<::fidl_test_simple::Simple> {
 public:
  explicit Server(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  Server(Server&& other) = delete;
  Server(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;

  ~Server() override { sync_completion_signal(destroyed_); }

  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    completer.Reply(request->request);
  }
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

 private:
  sync_completion_t* destroyed_;
};

TEST(BindTestCase, UniquePtrDestroyOnClientClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), std::move(server)));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(BindTestCase, UniquePtrDestroyOnServerClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), std::move(server)));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = fidl::WireCall<::fidl_test_simple::Simple>(zx::unowned_channel{local})->Close();
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(BindTestCase, CallbackDestroyOnClientClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelClosedFn<Server> cb = [](Server* server) { delete server; };

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), server.release(),
                                         std::move(cb)));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(BindTestCase, CallbackDestroyOnServerClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelClosedFn<Server> cb = [](Server* server) { delete server; };

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), server.release(),
                                         std::move(cb)));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = fidl::WireCall<::fidl_test_simple::Simple>(zx::unowned_channel{local})->Close();
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);

  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
}

TEST(BindTestCase, UnknownMethod) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), std::move(server)));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  // An epitaph is never a valid message to a server.
  fidl_epitaph_write(local.get(), ZX_OK);

  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// These classes are used to create a server implementation with multiple
// inheritance.
class PlaceholderBase1 {
 public:
  virtual void Foo() = 0;
  int a;
};

class PlaceholderBase2 {
 public:
  virtual void Bar() = 0;
  int b;
};

class MultiInheritanceServer : public PlaceholderBase1,
                               public fidl::WireServer<::fidl_test_simple::Simple>,
                               public PlaceholderBase2 {
 public:
  explicit MultiInheritanceServer(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  MultiInheritanceServer(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer(const MultiInheritanceServer& other) = delete;
  MultiInheritanceServer& operator=(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer& operator=(const MultiInheritanceServer& other) = delete;

  ~MultiInheritanceServer() override { sync_completion_signal(destroyed_); }

  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    completer.Reply(request->request);
  }
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

  void Foo() override {}
  void Bar() override {}

 private:
  sync_completion_t* destroyed_;
};

TEST(BindTestCase, MultipleInheritanceServer) {
  sync_completion_t destroyed;
  auto server = std::make_unique<MultiInheritanceServer>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(remote), std::move(server)));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = fidl::WireCall<::fidl_test_simple::Simple>(zx::unowned_channel{local})->Close();
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

}  // namespace
