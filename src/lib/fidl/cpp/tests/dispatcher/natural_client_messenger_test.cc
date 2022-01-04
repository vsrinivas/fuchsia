// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/message.h>

#include <array>

#include <zxtest/zxtest.h>

#include "test_messages.h"

namespace {

class TestProtocol {
 public:
  using Transport = fidl::internal::ChannelTransport;
  TestProtocol() = delete;
};

}  // namespace

namespace fidl {

template <>
class AsyncEventHandler<TestProtocol> : public fidl::internal::AsyncEventHandler {};

}  // namespace fidl

namespace {

// A fake client that supports capturing the messages sent to the server.
class FakeClientImpl : public fidl::internal::ClientBase {
 public:
  FakeClientImpl() {
    zx::status endpoints = fidl::CreateEndpoints<TestProtocol>();
    ZX_ASSERT(endpoints.is_ok());
    endpoints_ = std::move(endpoints.value());
  }

  size_t GetTransactionCount() { return fidl::internal::ClientBase::GetTransactionCount(); }

  fidl::Endpoints<TestProtocol>& endpoints() { return endpoints_; }

  template <typename T>
  void ReadFromServer(T callback) {
    return fidl::MessageRead(endpoints_.server.channel(), callback);
  }

 private:
  fidl::Endpoints<TestProtocol> endpoints_;
};

class FakeWireEventDispatcher
    : public fidl::internal::IncomingEventDispatcher<fidl::AsyncEventHandler<TestProtocol>> {
 public:
  FakeWireEventDispatcher() : IncomingEventDispatcher(nullptr) {}

 private:
  std::optional<fidl::UnbindInfo> DispatchEvent(
      fidl::IncomingMessage& msg,
      fidl::internal::IncomingTransportContext transport_context) override {
    ZX_PANIC("Never used in this test");
  }
};

constexpr uint64_t kTestOrdinal = 0x1234567812345678;

// A response context for recording errors and cancellation.
class MockResponseContext : public fidl::internal::ResponseContext {
 public:
  MockResponseContext() : fidl::internal::ResponseContext(kTestOrdinal) {}

  cpp17::optional<fidl::UnbindInfo> OnRawResult(
      ::fidl::IncomingMessage&& msg,
      fidl::internal::IncomingTransportContext transport_context) override {
    if (msg.ok()) {
      // We never get a response from the server in this test.
      ZX_PANIC("Never used in this test");
    }
    if (msg.reason() == fidl::Reason::kUnbind) {
      canceled_ = true;
      return cpp17::nullopt;
    }
    num_errors_ += 1;
    last_error_ = msg.error();
    return cpp17::nullopt;
  }

  bool canceled() const { return canceled_; }

  int num_errors() const { return num_errors_; }

  cpp17::optional<fidl::Result> last_error() const { return last_error_; }

 private:
  bool canceled_ = false;
  int num_errors_ = 0;
  cpp17::optional<fidl::Result> last_error_ = cpp17::nullopt;
};

class NaturalClientMessengerTest : public zxtest::Test {
 public:
  NaturalClientMessengerTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        controller_(Create(loop_.dispatcher())),
        impl_(static_cast<FakeClientImpl*>(&controller_.get())),
        messenger_(impl_) {}

 protected:
  async::Loop& loop() { return loop_; }
  FakeClientImpl* impl() { return impl_; }
  fidl::internal::ClientController& controller() { return controller_; }
  fidl::internal::NaturalClientMessenger& messenger() { return messenger_; }
  MockResponseContext& context() { return context_; }

 private:
  static fidl::internal::ClientController Create(async_dispatcher_t* dispatcher) {
    std::shared_ptr impl = std::make_shared<FakeClientImpl>();
    fidl::internal::ClientController controller;
    fidl::internal::AnyIncomingEventDispatcher event_dispatcher;
    event_dispatcher.emplace<FakeWireEventDispatcher>();
    controller.Bind(impl, fidl::internal::MakeAnyTransport(impl->endpoints().client.TakeChannel()),
                    dispatcher, std::move(event_dispatcher), fidl::AnyTeardownObserver::Noop(),
                    fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread);
    return controller;
  }

  async::Loop loop_;
  fidl::internal::ClientController controller_;
  FakeClientImpl* impl_;
  fidl::internal::NaturalClientMessenger messenger_;
  MockResponseContext context_;
};

TEST_F(NaturalClientMessengerTest, TwoWay) {
  GoodMessage good;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_EQ(0, context().num_errors());
  messenger().TwoWay(good.type(), good.message(), &context());
  loop().RunUntilIdle();
  EXPECT_EQ(1, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(0, context().num_errors());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_OK(incoming.status());
    EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
    EXPECT_NE(0, incoming.header()->txid);

    impl()->ForgetAsyncTxn(&context());
  });
}

TEST_F(NaturalClientMessengerTest, TwoWayInvalidMessage) {
  BadMessage too_large;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_EQ(0, context().num_errors());
  messenger().TwoWay(too_large.type(), too_large.message(), &context());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
  });

  loop().RunUntilIdle();
  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(1, context().num_errors());
  EXPECT_TRUE(context().last_error().has_value());
  EXPECT_EQ(fidl::Reason::kEncodeError, context().last_error()->reason());
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, context().last_error()->status());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());
  });
}

TEST_F(NaturalClientMessengerTest, TwoWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());

    EXPECT_EQ(0, impl()->GetTransactionCount());
    EXPECT_FALSE(context().canceled());
    EXPECT_EQ(0, context().num_errors());
    messenger().TwoWay(good.type(), good.message(), &context());
    loop().RunUntilIdle();
    EXPECT_EQ(0, impl()->GetTransactionCount());
    EXPECT_TRUE(context().canceled());
    EXPECT_EQ(0, context().num_errors());
    EXPECT_FALSE(context().last_error().has_value());
  });
}

TEST_F(NaturalClientMessengerTest, OneWay) {
  GoodMessage good;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Result result = messenger().OneWay(good.type(), good.message());
  loop().RunUntilIdle();
  EXPECT_OK(result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_OK(incoming.status());
    EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
    EXPECT_EQ(0, incoming.header()->txid);
  });
}

TEST_F(NaturalClientMessengerTest, OneWayInvalidMessage) {
  BadMessage too_large;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Result result = messenger().OneWay(too_large.type(), too_large.message());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
  });

  loop().RunUntilIdle();
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());
  });
}

TEST_F(NaturalClientMessengerTest, OneWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());

  impl()->ReadFromServer([&](fidl::IncomingMessage incoming,
                             fidl::internal::IncomingTransportContext incoming_transport_context) {
    EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());

    EXPECT_EQ(0, impl()->GetTransactionCount());
    fidl::Result result = messenger().OneWay(good.type(), good.message());

    loop().RunUntilIdle();
    EXPECT_EQ(ZX_ERR_CANCELED, result.status());
    EXPECT_EQ(0, impl()->GetTransactionCount());
  });
}

}  // namespace
