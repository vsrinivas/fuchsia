// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/message.h>

#include <array>

#include <zxtest/zxtest.h>

#include "test_messages.h"

namespace {

class TestProtocol {
  TestProtocol() = delete;
};

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

  std::optional<fidl::UnbindInfo> DispatchEvent(
      fidl::IncomingMessage& msg, fidl::internal::AsyncEventHandler* maybe_event_handler) override {
    ZX_PANIC("Never used in this test");
  }

  fidl::IncomingMessage ReadFromServer() {
    return fidl::ChannelReadEtc(endpoints_.server.channel().get(), 0,
                                fidl::BufferSpan(read_buffer_.data(), read_buffer_.size()),
                                cpp20::span<zx_handle_info_t>{});
  }

 private:
  fidl::Endpoints<TestProtocol> endpoints_;
  FIDL_ALIGNDECL std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES> read_buffer_;
};

constexpr uint64_t kTestOrdinal = 0x1234567812345678;

// A response context for recording errors and cancellation.
class MockResponseContext : public fidl::internal::ResponseContext {
 public:
  MockResponseContext() : fidl::internal::ResponseContext(kTestOrdinal) {}

  cpp17::optional<fidl::UnbindInfo> OnRawResult(::fidl::IncomingMessage&& msg) override {
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
        impl_(static_cast<FakeClientImpl*>(controller_.get())),
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
    controller.Bind(impl, impl->endpoints().client.TakeChannel(), dispatcher,
                    /* event_handler */ nullptr, fidl::AnyTeardownObserver::Noop(),
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

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_OK(incoming.status());
  EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
  EXPECT_NE(0, incoming.header()->txid);

  impl()->ForgetAsyncTxn(&context());
}

TEST_F(NaturalClientMessengerTest, TwoWayInvalidMessage) {
  BadMessage too_large;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_EQ(0, context().num_errors());
  messenger().TwoWay(too_large.type(), too_large.message(), &context());
  loop().RunUntilIdle();
  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(1, context().num_errors());
  EXPECT_TRUE(context().last_error().has_value());
  EXPECT_EQ(fidl::Reason::kEncodeError, context().last_error()->reason());
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, context().last_error()->status());

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
}

TEST_F(NaturalClientMessengerTest, TwoWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(0, context().num_errors());
  messenger().TwoWay(good.type(), good.message(), &context());
  loop().RunUntilIdle();
  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_TRUE(context().canceled());
  EXPECT_EQ(0, context().num_errors());
  EXPECT_FALSE(context().last_error().has_value());

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
}

TEST_F(NaturalClientMessengerTest, OneWay) {
  GoodMessage good;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Result result = messenger().OneWay(good.type(), good.message());
  loop().RunUntilIdle();
  EXPECT_OK(result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_OK(incoming.status());
  EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
  EXPECT_EQ(0, incoming.header()->txid);
}

TEST_F(NaturalClientMessengerTest, OneWayInvalidMessage) {
  BadMessage too_large;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Result result = messenger().OneWay(too_large.type(), too_large.message());
  loop().RunUntilIdle();
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
}

TEST_F(NaturalClientMessengerTest, OneWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Result result = messenger().OneWay(good.type(), good.message());
  loop().RunUntilIdle();
  EXPECT_EQ(ZX_ERR_CANCELED, result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  fidl::IncomingMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_SHOULD_WAIT, incoming.status());
}

}  // namespace
