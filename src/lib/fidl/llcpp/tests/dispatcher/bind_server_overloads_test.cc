// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.empty.protocol/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/sync/completion.h>

#include <optional>

#include <zxtest/zxtest.h>

namespace {

using ::test_empty_protocol::Empty;

class Server : public fidl::WireServer<Empty> {
 public:
  explicit Server(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  Server(Server&& other) = delete;
  Server(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;

  ~Server() override { sync_completion_signal(destroyed_); }

 private:
  sync_completion_t* destroyed_;
};

class BindServerOverloads : public zxtest::Test {
 public:
  BindServerOverloads()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        endpoints_(fidl::CreateEndpoints<Empty>()) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread());
    ASSERT_OK(endpoints_.status_value());
  }

  zx::result<fidl::Endpoints<Empty>>& endpoints() { return endpoints_; }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  async::Loop& loop() { return loop_; }

 private:
  async::Loop loop_;
  zx::result<fidl::Endpoints<Empty>> endpoints_;
};

// Test that |BindServer| correctly destroys a server it uniquely owns.
TEST_F(BindServerOverloads, UniquePtrWithoutUnboundHook) {
  sync_completion_t destroyed;
  auto result = fidl::BindServer(dispatcher(), std::move(endpoints()->server),
                                 std::make_unique<Server>(&destroyed));

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test that |BindServer| correctly destroys a server it uniquely owns,
// and that the |on_unbound| hook is executed before server destruction.
TEST_F(BindServerOverloads, UniquePtrWithUnboundHook) {
  sync_completion_t destroyed;
  sync_completion_t unbound;
  auto result = fidl::BindServer(
      dispatcher(), std::move(endpoints()->server), std::make_unique<Server>(&destroyed),
      [&unbound, &destroyed](Server*, fidl::UnbindInfo info, fidl::ServerEnd<Empty> server_end) {
        // Server is held alive by the runtime until we leave this lambda.
        ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

        sync_completion_signal(&unbound);
      });

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test that |BindServer| correctly destroys a server it uniquely owns
// via a |shared_ptr|.
TEST_F(BindServerOverloads, SharedPtrWithoutUnboundHook) {
  sync_completion_t destroyed;
  auto result = fidl::BindServer(dispatcher(), std::move(endpoints()->server),
                                 std::make_shared<Server>(&destroyed));

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test that |BindServer| correctly destroys a server it uniquely owns
// via a |shared_ptr|, and that the |on_unbound| hook is executed before
// server destruction.
TEST_F(BindServerOverloads, SharedPtrWithUnboundHook) {
  sync_completion_t destroyed;
  sync_completion_t unbound;
  auto result = fidl::BindServer(
      dispatcher(), std::move(endpoints()->server), std::make_shared<Server>(&destroyed),
      [&unbound, &destroyed](Server*, fidl::UnbindInfo info, fidl::ServerEnd<Empty> server_end) {
        // Server is held alive by the runtime until we leave this lambda.
        ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

        sync_completion_signal(&unbound);
      });

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test that |BindServer| does not prematurely destroy a server managed by
// |shared_ptr| when there are still outstanding references.
TEST_F(BindServerOverloads, SharedPtrWithUnboundHookAndSharedOwnership) {
  sync_completion_t destroyed;
  sync_completion_t unbound;
  auto shared_server = std::make_shared<Server>(&destroyed);
  auto result = fidl::BindServer(
      dispatcher(), std::move(endpoints()->server), shared_server,
      [&unbound, &destroyed](Server*, fidl::UnbindInfo info, fidl::ServerEnd<Empty> server_end) {
        // Server is held alive by the runtime until we leave this lambda.
        ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

        sync_completion_signal(&unbound);
      });

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));
  shared_server.reset();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test borrowing a server implementation with a raw pointer.
TEST_F(BindServerOverloads, RawPtrWithoutUnboundHook) {
  sync_completion_t destroyed;

  std::optional<Server> server{&destroyed};
  auto result = fidl::BindServer(dispatcher(), std::move(endpoints()->server), &server.value());

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

  loop().Shutdown();
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

  // Now it's safe to destroy |server|.
  server.reset();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

// Test borrowing a server implementation with a raw pointer, and supplying
// an |on_unbound| hook.
TEST_F(BindServerOverloads, RawPtrWithUnboundHook) {
  sync_completion_t destroyed;
  sync_completion_t unbound;

  std::optional<Server> server{&destroyed};
  auto result = fidl::BindServer(
      dispatcher(), std::move(endpoints()->server), &server.value(),
      [&unbound, &destroyed](Server*, fidl::UnbindInfo info, fidl::ServerEnd<Empty> server_end) {
        // Server is held alive by the local variable.
        ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

        sync_completion_signal(&unbound);
      });

  // Trigger binding destruction before loop's destruction.
  endpoints()->client.reset();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

  loop().Shutdown();
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

  // Now it's safe to destroy |server|.
  server.reset();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

}  // namespace
