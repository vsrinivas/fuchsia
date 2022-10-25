// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.basic.protocol/cpp/wire.h>
#include <fidl/test.empty.protocol/cpp/wire.h>
#include <lib/sync/cpp/completion.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/lib/fidl/llcpp/tests/dispatcher/async_loop_and_endpoints_fixture.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/lsan_disabler.h"

namespace {

class ServerBindingTest : public zxtest::Test {
 public:
  ServerBindingTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    zx::result endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
    ASSERT_OK(endpoints.status_value());
    endpoints_ = std::move(*endpoints);
  }

  async::Loop& loop() { return loop_; }

  fidl::Endpoints<test_basic_protocol::ValueEcho>& endpoints() { return endpoints_; }

 private:
  async::Loop loop_;
  fidl::Endpoints<test_basic_protocol::ValueEcho> endpoints_;
};

struct Server : public fidl::WireServer<test_basic_protocol::ValueEcho> {
  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    call_count++;
    completer.Reply(request->s);
  }

  size_t call_count = 0;
};

TEST_F(ServerBindingTest, Control) {
  Server server;
  bool close_handler_called = false;
  {
    fidl::ServerBinding<test_basic_protocol::ValueEcho> binding{
        loop().dispatcher(), std::move(endpoints().server), &server,
        [&close_handler_called](fidl::UnbindInfo) { close_handler_called = true; }};

    constexpr static const char kPayload[] = "test";
    fidl::WireClient client(std::move(endpoints().client), loop().dispatcher());

    EXPECT_EQ(0u, server.call_count);
    {
      client->Echo(kPayload).ThenExactlyOnce(
          [](fidl::WireUnownedResult<test_basic_protocol::ValueEcho::Echo>& result) {
            ASSERT_TRUE(result.ok());
            EXPECT_EQ(kPayload, result->s.get());
          });
      loop().RunUntilIdle();
      EXPECT_EQ(1u, server.call_count);
    }
    {
      client->Echo(kPayload).ThenExactlyOnce(
          [](fidl::WireUnownedResult<test_basic_protocol::ValueEcho::Echo>& result) {
            ASSERT_TRUE(result.ok());
            EXPECT_EQ(kPayload, result->s.get());
          });
      loop().RunUntilIdle();
      EXPECT_EQ(2u, server.call_count);
    }

    // Unbind at end of scope. |binding| is destroyed here.
  }
  loop().RunUntilIdle();

  // Unbind does not call CloseHandler.
  EXPECT_FALSE(close_handler_called);
}

TEST_F(ServerBindingTest, CloseHandler) {
  Server server;
  std::optional<fidl::UnbindInfo> error;
  int close_handler_count = 0;
  fidl::ServerBinding<test_basic_protocol::ValueEcho> binding{
      loop().dispatcher(), std::move(endpoints().server), &server,
      [&close_handler_count, &error](fidl::UnbindInfo info) {
        error.emplace(info);
        close_handler_count++;
      }};

  endpoints().client.reset();
  loop().RunUntilIdle();

  EXPECT_TRUE(error.has_value());
  EXPECT_TRUE(error->is_peer_closed());
  EXPECT_EQ(1, close_handler_count);
}

TEST_F(ServerBindingTest, CloseBindingCallsTheCloseHandler) {
  Server server;
  std::optional<fidl::UnbindInfo> error;
  int close_handler_count = 0;
  fidl::ServerBinding<test_basic_protocol::ValueEcho> binding{
      loop().dispatcher(), std::move(endpoints().server), &server,
      [&close_handler_count, &error](fidl::UnbindInfo info) {
        error.emplace(info);
        close_handler_count++;
      }};

  binding.Close(ZX_OK);
  loop().RunUntilIdle();

  EXPECT_TRUE(error.has_value());
  EXPECT_TRUE(error->is_user_initiated());
  EXPECT_EQ(error->reason(), fidl::Reason::kClose);
  EXPECT_EQ(1, close_handler_count);
}

TEST_F(ServerBindingTest, BindingDestructionPassivatesTheCloseHandler) {
  Server server;
  int close_handler_count = 0;
  auto binding = std::make_optional<fidl::ServerBinding<test_basic_protocol::ValueEcho>>(
      loop().dispatcher(), std::move(endpoints().server), &server,
      [&close_handler_count](fidl::UnbindInfo info) { close_handler_count++; });

  endpoints().client.reset();
  binding.reset();

  loop().RunUntilIdle();

  EXPECT_EQ(0, close_handler_count);
}

TEST_F(ServerBindingTest, DestructDuringCloseHandler) {
  Server server;
  int close_handler_count = 0;
  std::unique_ptr<fidl::ServerBinding<test_basic_protocol::ValueEcho>> binding;
  binding = std::make_unique<fidl::ServerBinding<test_basic_protocol::ValueEcho>>(
      loop().dispatcher(), std::move(endpoints().server), &server,
      [&close_handler_count, &binding](fidl::UnbindInfo info) {
        close_handler_count++;
        // Destroying the binding here should be allowed.
        binding.reset();
      });

  endpoints().client.reset();
  loop().RunUntilIdle();

  EXPECT_EQ(1, close_handler_count);
}

TEST(ServerBindingTest, CannotDestroyOnAnotherThread) {
  fidl_testing::RunWithLsanDisabled([&] {
    Server server;
    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx::result endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
    ASSERT_OK(endpoints.status_value());

    auto binding = std::make_unique<fidl::ServerBinding<test_basic_protocol::ValueEcho>>(
        loop->dispatcher(), std::move(endpoints->server), &server, [](fidl::UnbindInfo info) {});

    // Panics when a foreign thread attempts to destroy the binding.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    std::thread foreign_thread(
        [&] { ASSERT_DEATH([&] { fidl_testing::RunWithLsanDisabled([&] { binding = {}; }); }); });
    foreign_thread.join();
    // The above thread will not be able to finish unbinding -- it would be
    // terminated due to exception. That puts the binding in a corrupted state
    // where the terminated thread held a reference count to the internal
    // binding that will never be dropped. To workaround that we just leak the
    // loop and skip any teardown.
    loop.release();
#endif
  });
}

TEST(ServerBindingTest, CannotCloseOnAnotherThread) {
  fidl_testing::RunWithLsanDisabled([&] {
    Server server;
    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx::result endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
    ASSERT_OK(endpoints.status_value());

    auto binding = std::make_unique<fidl::ServerBinding<test_basic_protocol::ValueEcho>>(
        loop->dispatcher(), std::move(endpoints->server), &server, [](fidl::UnbindInfo info) {});

    // Panics when a foreign thread attempts to close the binding.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    std::thread foreign_thread([&] {
      ASSERT_DEATH([&] { fidl_testing::RunWithLsanDisabled([&] { binding->Close(ZX_OK); }); });
    });
    foreign_thread.join();
    // The above thread will not be able to finish unbinding -- it would be
    // terminated due to exception. That puts the binding in a corrupted state
    // where the terminated thread held a reference count to the internal
    // binding that will never be dropped. To workaround that we just leak the
    // loop and skip any teardown.
    loop.release();
#endif
  });
}
}  // namespace
