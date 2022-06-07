// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/hosting/singleton_fidl_server.h"

#include <fuchsia/examples/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace fmlib {
namespace {

class SingletonFidlServerTest : public gtest::RealLoopFixture {
 public:
  SingletonFidlServerTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {}

  // The thread on which this test was created.
  Thread& thread() { return thread_; }

 private:
  Thread thread_;
};

class TestServer : public SingletonFidlServer<fuchsia::examples::Echo> {
 public:
  static size_t instance_count() { return instance_count_; }

  explicit TestServer(bool bind_now) : SingletonFidlServer<fuchsia::examples::Echo>(bind_now) {
    ++instance_count_;
  }

  ~TestServer() override { --instance_count_; }

  void CompleteDeferredBinding() {
    SingletonFidlServer<fuchsia::examples::Echo>::CompleteDeferredBinding();
  }

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
  static size_t instance_count_;
  std::optional<std::tuple<std::string, EchoStringCallback>> echo_string_args_;
  std::optional<std::string> send_string_args_;
};

size_t TestServer::instance_count_ = 0;

const std::string kString = "test_string";
const std::string kStringA = "test_string_a";
const std::string kStringB = "test_string_b";
constexpr char kThreadName[] = "test_thread";

// Tests launching of a singleton fidl server using |ServiceProvider|. Makes two connections
// sequentially to verify that the server is destroyed and recreated as expected.
TEST_F(SingletonFidlServerTest, Register) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr = nullptr;
  SingletonFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(),
      [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        impl_ptr = server.get();
        return server;
      },
      /* destroy_when_unbound */ true);

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

  // Connect again.
  impl_ptr = nullptr;
  echo_ptr = service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect a server to be created.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Verify that |impl_ptr| and |echo_ptr| refer to the same server by calling a FIDL method.
  echo_ptr->SendString(kString);
  RunLoopUntilIdle();
  send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());

  // Drop the connection and expect the server to be deleted.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests launching of a singleton fidl server using |ServiceProvider| using the overload of
// |Register| that creates servers on their own threads.
TEST_F(SingletonFidlServerTest, RegisterOwnThread) {
  ServiceProvider service_provider(thread());

  bool server_created = false;
  SingletonFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, kThreadName,
      [this, &server_created](Thread thread) {
        EXPECT_FALSE(this->thread().is_current());
        server_created = true;
        return std::make_unique<TestServer>(/* bind_now */ true);
      },
      /* destroy_when_unbound */ true);

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

// Tests launching of a singleton fidl server using |ServiceProvider|, ensuring that only one
// instance of the service implementation is created for two connections.
TEST_F(SingletonFidlServerTest, ActuallySingleton) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr = nullptr;
  SingletonFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(),
      [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        EXPECT_FALSE(!!impl_ptr);
        impl_ptr = server.get();
        return server;
      },
      /* destroy_when_unbound */ true);

  fuchsia::examples::EchoPtr echo_ptr_a =
      service_provider.ConnectToService<fuchsia::examples::Echo>();
  fuchsia::examples::EchoPtr echo_ptr_b =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect one server to be created.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
  EXPECT_NE(nullptr, impl_ptr);

  // Verify that |impl_ptr| and |echo_ptr_a| refer to the same server by calling a FIDL method.
  echo_ptr_a->SendString(kStringA);
  RunLoopUntilIdle();
  auto send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kStringA, send_string_args.value());

  // Verify that |impl_ptr| and |echo_ptr_b| refer to the same server by calling a FIDL method.
  echo_ptr_b->SendString(kStringB);
  RunLoopUntilIdle();
  send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kStringB, send_string_args.value());

  // Drop the 'a' connection and expect the server to be deleted.
  echo_ptr_a = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());

  // Drop the 'b' connection and expect a server to be deleted.
  echo_ptr_b = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
}

// Tests launching of a singleton fidl server using |ServiceProvider| and passing false for
// |destroy_when_unbound|.
TEST_F(SingletonFidlServerTest, Immortal) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr = nullptr;
  SingletonFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(),
      [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ true);
        impl_ptr = server.get();
        return server;
      },
      /* destroy_when_unbound */ false);

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

  // Drop the connection and expect the server to remain.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());

  // Connect again..
  echo_ptr = service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect the server to remain.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());

  // Verify that |impl_ptr| and |echo_ptr| refer to the same server by calling a FIDL method.
  echo_ptr->SendString(kString);
  RunLoopUntilIdle();
  send_string_args = impl_ptr->TakeSendStringArgs();
  EXPECT_TRUE(send_string_args.has_value());
  EXPECT_EQ(kString, send_string_args.value());

  // Drop the connection and expect the server to remain.
  echo_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(1u, TestServer::instance_count());
}

// Tests launching of a singleton fidl server using deferred binding.
TEST_F(SingletonFidlServerTest, DeferBind) {
  ServiceProvider service_provider(thread());

  TestServer* impl_ptr = nullptr;
  SingletonFidlServer<fuchsia::examples::Echo>::Register(
      service_provider, thread(),
      [&impl_ptr](Thread thread) {
        auto server = std::make_unique<TestServer>(/* bind_now */ false);
        impl_ptr = server.get();
        return server;
      },
      /* destroy_when_unbound */ true);

  // Expect no server has been instantiated, because we haven't connected yet.
  RunLoopUntilIdle();
  EXPECT_EQ(0u, TestServer::instance_count());
  EXPECT_EQ(nullptr, impl_ptr);

  // Connect.
  fuchsia::examples::EchoPtr echo_ptr =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  // Expect a server has been created.
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

}  // namespace
}  // namespace fmlib
