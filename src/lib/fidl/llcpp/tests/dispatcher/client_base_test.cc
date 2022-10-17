// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/message.h>

#include <array>

#include <zxtest/zxtest.h>

namespace {

class TestProtocol {
 public:
  using Transport = fidl::internal::ChannelTransport;
  TestProtocol() = delete;
};

constexpr uint64_t kTestOrdinal = 0x1234567812345678;

// |GoodMessage| is a helper to create a valid FIDL transactional message.
class GoodMessage {
 public:
  GoodMessage() : message_(MakeMessage(&content_)) {
    fidl::InitTxnHeader(&content_, 0, kTestOrdinal, fidl::MessageDynamicFlags::kStrictMethod);
  }

  fidl::OutgoingMessage& message() { return message_; }

 private:
  static fidl::OutgoingMessage MakeMessage(fidl_message_header_t* content) {
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
        .byte =
            {
                .bytes = reinterpret_cast<uint8_t*>(content),
                .num_bytes = sizeof(content_),
            },
    };
    return fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  }

  FIDL_ALIGNDECL fidl_message_header_t content_ = {};
  fidl::OutgoingMessage message_;
};

}  // namespace

namespace fidl {

template <>
class WireAsyncEventHandler<TestProtocol> : public fidl::internal::AsyncEventHandler,
                                            public fidl::internal::BaseEventHandlerInterface {};

}  // namespace fidl

namespace {

// A fake client that supports capturing the messages sent to the server.
class FakeClientImpl {
 public:
  explicit FakeClientImpl(fidl::internal::ClientBase* client_base,
                          fidl::ServerEnd<TestProtocol> server_end)
      : client_base_(client_base), server_end_(std::move(server_end)) {}

  size_t GetTransactionCount() { return client_base_->GetTransactionCount(); }

  void ForgetAsyncTxn(fidl::internal::ResponseContext* context) {
    return client_base_->ForgetAsyncTxn(context);
  }

  fidl::ServerEnd<TestProtocol>& server_end() { return server_end_; }

  fidl::IncomingHeaderAndMessage ReadFromServer() {
    return fidl::MessageRead(
        server_end_.channel(),
        fidl::ChannelMessageStorageView{
            .bytes =
                fidl::BufferSpan(read_buffer_.data(), static_cast<uint32_t>(read_buffer_.size())),
            .handles = nullptr,
            .handle_metadata = nullptr,
            .handle_capacity = 0,
        });
  }

 private:
  fidl::internal::ClientBase* client_base_;
  fidl::ServerEnd<TestProtocol> server_end_;
  FIDL_ALIGNDECL std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES> read_buffer_;
};

class FakeWireEventDispatcher
    : public fidl::internal::IncomingEventDispatcher<fidl::WireAsyncEventHandler<TestProtocol>> {
 public:
  FakeWireEventDispatcher() : IncomingEventDispatcher(nullptr) {}

 private:
  fidl::Status DispatchEvent(fidl::IncomingHeaderAndMessage& msg,
                             fidl::internal::MessageStorageViewBase* storage_view) override {
    ZX_PANIC("Never used in this test");
  }
};

// A response context for recording errors and cancellation.
class MockResponseContext : public fidl::internal::ResponseContext {
 public:
  MockResponseContext() : fidl::internal::ResponseContext(kTestOrdinal) {}

  std::optional<fidl::UnbindInfo> OnRawResult(
      ::fidl::IncomingHeaderAndMessage&& msg,
      fidl::internal::MessageStorageViewBase* storage_view) override {
    if (msg.ok()) {
      // We never get a response from the server in this test.
      ZX_PANIC("Never used in this test");
    }
    if (msg.reason() == fidl::Reason::kUnbind) {
      canceled_ = true;
      return std::nullopt;
    }
    num_errors_ += 1;
    last_error_ = msg.error();
    return std::nullopt;
  }

  bool canceled() const { return canceled_; }

  int num_errors() const { return num_errors_; }

  std::optional<fidl::Status> last_error() const { return last_error_; }

 private:
  bool canceled_ = false;
  int num_errors_ = 0;
  std::optional<fidl::Status> last_error_ = std::nullopt;
};

class ClientBaseTest : public zxtest::Test {
 public:
  ClientBaseTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    zx::result endpoints = fidl::CreateEndpoints<TestProtocol>();
    ZX_ASSERT(endpoints.is_ok());

    fidl::internal::AnyIncomingEventDispatcher event_dispatcher;
    event_dispatcher.emplace<FakeWireEventDispatcher>();
    controller_.Bind(fidl::internal::MakeAnyTransport(endpoints->client.TakeChannel()),
                     loop_.dispatcher(), std::move(event_dispatcher), nullptr,
                     fidl::AnyTeardownObserver::Noop(),
                     fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread);

    impl_ = std::make_unique<FakeClientImpl>(&controller_.get(), std::move(endpoints->server));
  }

 protected:
  async::Loop& loop() { return loop_; }
  FakeClientImpl* impl() { return impl_.get(); }
  fidl::internal::ClientController& controller() { return controller_; }
  fidl::internal::ClientBase& client_base() { return controller_.get(); }
  MockResponseContext& context() { return context_; }

 private:
  async::Loop loop_;
  fidl::internal::ClientController controller_;
  std::unique_ptr<FakeClientImpl> impl_;
  MockResponseContext context_;
};

TEST_F(ClientBaseTest, TwoWay) {
  GoodMessage good;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_EQ(0, context().num_errors());
  client_base().SendTwoWay(good.message(), &context());
  loop().RunUntilIdle();
  EXPECT_EQ(1, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(0, context().num_errors());

  fidl::IncomingHeaderAndMessage incoming = impl()->ReadFromServer();
  EXPECT_OK(incoming.status());
  EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
  EXPECT_NE(0, incoming.header()->txid);

  impl()->ForgetAsyncTxn(&context());
}

TEST_F(ClientBaseTest, TwoWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());

  fidl::IncomingHeaderAndMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());

  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_FALSE(context().canceled());
  EXPECT_EQ(0, context().num_errors());
  client_base().SendTwoWay(good.message(), &context());
  loop().RunUntilIdle();
  EXPECT_EQ(0, impl()->GetTransactionCount());
  EXPECT_TRUE(context().canceled());
  EXPECT_EQ(0, context().num_errors());
  EXPECT_FALSE(context().last_error().has_value());
}

TEST_F(ClientBaseTest, OneWay) {
  GoodMessage good;

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Status result = client_base().SendOneWay(good.message());
  loop().RunUntilIdle();
  EXPECT_OK(result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());

  fidl::IncomingHeaderAndMessage incoming = impl()->ReadFromServer();
  EXPECT_OK(incoming.status());
  EXPECT_EQ(kTestOrdinal, incoming.header()->ordinal);
  EXPECT_EQ(0, incoming.header()->txid);
}

TEST_F(ClientBaseTest, OneWayUnbound) {
  GoodMessage good;

  controller().Unbind();
  ASSERT_OK(loop().RunUntilIdle());

  fidl::IncomingHeaderAndMessage incoming = impl()->ReadFromServer();
  EXPECT_STATUS(ZX_ERR_PEER_CLOSED, incoming.status());

  EXPECT_EQ(0, impl()->GetTransactionCount());
  fidl::Status result = client_base().SendOneWay(good.message());

  loop().RunUntilIdle();
  EXPECT_EQ(ZX_ERR_CANCELED, result.status());
  EXPECT_EQ(0, impl()->GetTransactionCount());
}

}  // namespace
