// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/hosting/nonce_fidl_server.h"

#include <fuchsia/examples/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/hosting/service_provider.h"

namespace fmlib {
namespace {

class NonceFidlServerTest : public gtest::RealLoopFixture {
 public:
  NonceFidlServerTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {}

  // The thread on which this test was created.
  Thread& thread() { return thread_; }

 private:
  Thread thread_;
};

class TestServer : public NonceFidlServer<fuchsia::examples::Echo> {
 public:
  static size_t instance_count() { return instance_count_; }

  explicit TestServer(bool bind_now) : NonceFidlServer<fuchsia::examples::Echo>(bind_now) {
    ++instance_count_;
  }

  ~TestServer() override { --instance_count_; }

  using NonceFidlServer<fuchsia::examples::Echo>::CompleteDeferredBinding;

  using NonceFidlServer<fuchsia::examples::Echo>::Unbind;

  std::optional<std::tuple<std::string, EchoStringCallback>> TakeEchoStringArgs() {
    std::optional<std::tuple<std::string, EchoStringCallback>> result =
        std::move(echo_string_args_);
    echo_string_args_.reset();
    return result;
  }

  std::optional<std::string> TakeSendStringArgs() {
    std::optional<std::string> result = std::move(send_string_args_);
    send_string_args_.reset();
    return result;
  }

  // fuchsia::examples::Echo implementation.
  void EchoString(std::string value, EchoStringCallback callback) override {
    echo_string_args_ =
        std::make_tuple<std::string, EchoStringCallback>(std::move(value), std::move(callback));
  }

  void SendString(std::string value) override { send_string_args_ = std::move(value); }

 private:
  static std::atomic_size_t instance_count_;
  std::optional<std::tuple<std::string, EchoStringCallback>> echo_string_args_;
  std::optional<std::string> send_string_args_;
};

std::atomic_size_t TestServer::instance_count_ = 0;

const std::string kString = "test_string";
constexpr char kThreadName[] = "test_thread";
constexpr zx_status_t kErrorStatus = ZX_ERR_INVALID_ARGS;

// Tests launching of a nonce fidl server using |NonceFidlServer::Launch|.
TEST_F(NonceFidlServerTest, Launch) {
  fuchsia::examples::EchoPtr echo_ptr;
  TestServer* impl_ptr = nullptr;
  NonceFidlServer<fuchsia::examples::Echo>::Launch(
      thread(), echo_ptr.NewRequest(), [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        impl_ptr = server.get();
        return server;
      });

  // Launch is deferred using the task queue, so don't expect to have a server yet.
  EXPECT_EQ(0u, TestServer::instance_count());
  EXPECT_EQ(nullptr, impl_ptr);

  // Expect a server after the task has a chance to run.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Verify that |impl_ptr| and |echo_ptr| refer to the same server by calling a FIDL method.
  echo_ptr->SendString(kString);
  RunLoopUntilIdle();
  auto send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());

  // Drop the connection and expect the server to be deleted.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests launching of a nonce fidl server using |NonceFidlServer::Launch| with deferred binding.
TEST_F(NonceFidlServerTest, LaunchDeferBind) {
  fuchsia::examples::EchoPtr echo_ptr;
  TestServer* impl_ptr = nullptr;
  NonceFidlServer<fuchsia::examples::Echo>::Launch(
      thread(), echo_ptr.NewRequest(), [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ false);
        impl_ptr = server.get();
        return server;
      });

  // Launch is deferred using the task queue, so don't expect to have a server yet.
  EXPECT_EQ(0u, TestServer::instance_count());
  EXPECT_EQ(nullptr, impl_ptr);

  // Expect a server after the task has a chance to run.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Call a fidl method, but don't expect it to be handled, because no binding has occurred.
  echo_ptr->SendString(kString);
  RunLoopUntilIdle();
  EXPECT_FALSE(impl_ptr->TakeSendStringArgs().has_value());

  // Complete the binding, and expect that the method was called.
  impl_ptr->CompleteDeferredBinding();
  RunLoopUntilIdle();
  auto send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());

  // Drop the connection and expect the server to be deleted.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests |NonceFidlServer::Unbind|.
TEST_F(NonceFidlServerTest, Unbind) {
  fuchsia::examples::EchoPtr echo_ptr;
  TestServer* impl_ptr = nullptr;
  NonceFidlServer<fuchsia::examples::Echo>::Launch(
      thread(), echo_ptr.NewRequest(), [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        impl_ptr = server.get();
        return server;
      });

  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Set up an error handler for the channel, and expect that it doesn't run immediately.
  std::optional<zx_status_t> error_status;
  echo_ptr.set_error_handler([&error_status](zx_status_t status) { error_status = status; });
  RunLoopUntilIdle();
  EXPECT_FALSE(error_status.has_value());

  // Tell the server to call |Unbind| and expect the error handler to run, passing the same
  // status passed to |Unbind|.
  impl_ptr->Unbind(kErrorStatus);
  RunLoopUntilIdle();
  EXPECT_TRUE(error_status.has_value());
  EXPECT_EQ(kErrorStatus, error_status.value());
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests launching of a nonce fidl server using |ServiceProvider|.
TEST_F(NonceFidlServerTest, Register) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr = nullptr;
  NonceFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(), [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        impl_ptr = server.get();
        return server;
      });

  // Expect no server has been instantiated, because we haven't connected yet.
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
  EXPECT_EQ(nullptr, impl_ptr);

  // Connect.
  fuchsia::examples::EchoPtr echo_ptr =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect a server to be created.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Verify that |impl_ptr| and |echo_ptr| refer to the same server by calling a FIDL method.
  echo_ptr->SendString(kString);
  RunLoopUntilIdle();
  auto send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());

  // Drop the connection and expect the server to be deleted.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests launching of a nonce fidl server using |ServiceProvider| using the overload of
// |Register| that creates servers on their own threads.
TEST_F(NonceFidlServerTest, RegisterOwnThread) {
  ServiceProvider service_provider(thread());

  bool server_created = false;
  NonceFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, kThreadName, [this, &server_created](Thread thread) {
        EXPECT_FALSE(this->thread().is_current());
        server_created = true;
        return std::make_unique<TestServer>(/* bind_now */ true);
      });

  // Expect no server has been instantiated, because we haven't connected yet.
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());

  // Connect.
  fuchsia::examples::EchoPtr echo_ptr =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect a server to be created when the new thread gets around to it.
  RunLoopUntil([]() { return TestServer::instance_count() == 1; });

  // Drop the connection and expect the server to be deleted when the new thread gets around to it.
  echo_ptr = nullptr;
  RunLoopUntil([]() { return TestServer::instance_count() == 0; });
}

// Tests launching of two nonce fidl servers using |ServiceProvider|, ensuring that an instance
// of the service implementation is created for each connection.
TEST_F(NonceFidlServerTest, ActuallyNonce) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr_a = nullptr;
  TestServer* impl_ptr_b = nullptr;
  NonceFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(), [&impl_ptr_a, &impl_ptr_b](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);

        if (!impl_ptr_a) {
          impl_ptr_a = server.get();
        } else {
          EXPECT_FALSE(!!impl_ptr_b);
          impl_ptr_b = server.get();
        }

        return server;
      });

  fuchsia::examples::EchoPtr echo_ptr_a =
      service_provider.ConnectToService<fuchsia::examples::Echo>();
  fuchsia::examples::EchoPtr echo_ptr_b =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect two servers to be created.
  RunLoopUntilIdle();
  EXPECT_EQ(2u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr_a);
  EXPECT_NE(nullptr, impl_ptr_b);

  // Verify that |impl_ptr_a| and |echo_ptr_a| refer to the same server by calling a FIDL method.
  echo_ptr_a->SendString(kString);
  RunLoopUntilIdle();
  auto send_string_args = impl_ptr_a->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());
  EXPECT_FALSE(impl_ptr_b->TakeSendStringArgs().has_value());

  // Verify that |impl_ptr_b| and |echo_ptr_b| refer to the same server by calling a FIDL method.
  echo_ptr_b->SendString(kString);
  RunLoopUntilIdle();
  send_string_args = impl_ptr_b->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());
  EXPECT_FALSE(impl_ptr_a->TakeSendStringArgs().has_value());

  // Drop the 'a' connection and expect a server to be deleted.
  echo_ptr_a = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());

  // Drop the 'b' connection and expect a server to be deleted.
  echo_ptr_b = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

}  // namespace
}  // namespace fmlib
