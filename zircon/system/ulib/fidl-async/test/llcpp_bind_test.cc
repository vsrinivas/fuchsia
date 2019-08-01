// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

#include "generated/fidl_llcpp_simple.test.h"

namespace {

class Server : public ::llcpp::fidl::test::simple::Simple::Interface {
 public:
  explicit Server(std::atomic<size_t>* destroyed) : destroyed_(destroyed) {}
  Server(Server&& other) = delete;
  Server(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;

  ~Server() override { destroyed_->fetch_add(1); }

  void Close(CloseCompleter::Sync completer) override { completer.Close(ZX_OK); }

 private:
  std::atomic<size_t>* destroyed_;
};

TEST(BindTestCase, UniquePtrDestroyOnClientClose) {
  std::atomic<size_t> destroyed = 0;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(remote), std::move(server)));
  loop.RunUntilIdle();
  ASSERT_EQ(destroyed.load(), 0);

  local.reset();
  loop.RunUntilIdle();
  ASSERT_EQ(destroyed.load(), 1);
}

TEST(BindTestCase, UniquePtrDestroyOnServerClose) {
  std::atomic<size_t> destroyed = 0;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(remote), std::move(server)));
  ASSERT_EQ(destroyed.load(), 0);

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
  ASSERT_EQ(destroyed.load(), 1);
}

TEST(BindTestCase, CallbackDestroyOnClientClose) {
  std::atomic<size_t> destroyed = 0;
  std::atomic<size_t> callback_called = 0;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelClosedFn<Server> cb = [&callback_called](Server* server) {
    callback_called.fetch_add(1);
    delete server;
  };

  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(remote), server.release(), std::move(cb)));
  loop.RunUntilIdle();
  ASSERT_EQ(callback_called.load(), 0);
  ASSERT_EQ(destroyed.load(), 0);

  local.reset();
  loop.RunUntilIdle();
  ASSERT_EQ(callback_called.load(), 1);
  ASSERT_EQ(destroyed.load(), 1);
}

TEST(BindTestCase, CallbackDestroyOnServerClose) {
  std::atomic<size_t> destroyed = 0;
  auto server = std::make_unique<Server>(&destroyed);
  std::atomic<size_t> callback_called = 0;
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelClosedFn<Server> cb = [&callback_called](Server* server) {
    callback_called.fetch_add(1);
    delete server;
  };

  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(remote), server.release(), std::move(cb)));
  ASSERT_EQ(destroyed.load(), 0);
  ASSERT_EQ(callback_called.load(), 0);

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
  ASSERT_EQ(destroyed.load(), 1);
  ASSERT_EQ(callback_called.load(), 1);
}

}  // namespace
