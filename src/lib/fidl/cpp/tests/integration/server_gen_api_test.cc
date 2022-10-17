// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.basic.protocol/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/wire/server.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

using ::test_basic_protocol::ValueEcho;

constexpr char kExpectedReply[] = "test";

TEST(Server, SyncReply) {
  struct SyncServer : fidl::Server<ValueEcho> {
    void Echo(EchoRequest& request, EchoCompleter::Sync& completer) override {
      EXPECT_TRUE(completer.is_reply_needed());
      completer.Reply(request.s());
      EXPECT_FALSE(completer.is_reply_needed());
    }
  };

  auto server = std::make_unique<SyncServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  zx::result endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());

  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());

  std::thread call([&] {
    fidl::WireResult result = fidl::WireCall(endpoints->client)->Echo(kExpectedReply);
    EXPECT_OK(result.status());
    EXPECT_EQ(result.value().s.get(), kExpectedReply);
    loop.Quit();
  });
  EXPECT_STATUS(ZX_ERR_CANCELED, loop.Run());
  call.join();
}

TEST(Server, AsyncReply) {
  struct AsyncServer : fidl::Server<ValueEcho> {
    void Echo(EchoRequest& request, EchoCompleter::Sync& completer) override {
      worker_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      async::PostTask(worker_loop_->dispatcher(),
                      [request = request.s(), completer = completer.ToAsync()]() mutable {
                        EXPECT_TRUE(completer.is_reply_needed());
                        completer.Reply(request);
                        EXPECT_FALSE(completer.is_reply_needed());
                      });
      EXPECT_FALSE(completer.is_reply_needed());
      ASSERT_OK(worker_loop_->StartThread());
    }
    std::unique_ptr<async::Loop> worker_loop_;
  };

  auto server = std::make_unique<AsyncServer>();
  async::Loop main_loop(&kAsyncLoopConfigNeverAttachToThread);
  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());

  fidl::BindServer(main_loop.dispatcher(), std::move(endpoints->server), server.get());

  std::thread call([&] {
    auto result = fidl::WireCall(endpoints->client)->Echo(kExpectedReply);
    EXPECT_OK(result.status());
    EXPECT_EQ(result.value().s.get(), kExpectedReply);
    main_loop.Quit();
  });
  EXPECT_STATUS(ZX_ERR_CANCELED, main_loop.Run());
  call.join();
}

}  // namespace
