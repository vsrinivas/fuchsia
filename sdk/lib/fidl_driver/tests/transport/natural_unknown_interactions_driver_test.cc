// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.unknown.interactions/cpp/driver/fidl.h>
#include <fidl/test.unknown.interactions/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sync/cpp/completion.h>

#include <algorithm>
#include <future>
#include <optional>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"

struct FakeUnknownMethod {
  static constexpr uint64_t kOrdinal = 0x10ff10ff10ff10ff;
};

template <>
struct ::fidl::internal::WireOrdinal<FakeUnknownMethod> {
  static constexpr uint64_t value = FakeUnknownMethod::kOrdinal;
};

template <>
struct ::fidl::TypeTraits<::fidl::WireResponse<FakeUnknownMethod>> {
  static constexpr uint32_t kPrimarySize = sizeof(fidl_xunion_v2_t);
  static constexpr uint32_t kMaxOutOfLine = 0;
};

namespace {
namespace test = ::test_unknown_interactions;

class TestServerBase {
 public:
  virtual ~TestServerBase() = default;

  void run_server_assertions() {
    std::lock_guard lock(server_assertions_lock);
    for (auto& server_assertion : server_assertions) {
      server_assertion();
    }
  }

 protected:
  void add_server_assertion(fit::function<void()>&& assertion) {
    std::lock_guard lock(server_assertions_lock);
    server_assertions.emplace_back(std::move(assertion));
  }

 private:
  // ADD_FAILURE and ASSERT/EXPECT can be racy, so run assertions from the
  // server side on tear-down instead of on the dispatcher thread.
  std::vector<fit::function<void()>> server_assertions;
  std::mutex server_assertions_lock;
};

class UnknownInteractionsServerBase
    : public ::fdf::Server<::test::UnknownInteractionsDriverProtocol>,
      public TestServerBase {
  void StrictOneWay(StrictOneWayCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("StrictOneWay called unexpectedly"); });
  }

  void FlexibleOneWay(FlexibleOneWayCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("FlexibleOneWay called unexpectedly"); });
  }

  void StrictTwoWay(StrictTwoWayCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("StrictTwoWay called unexpectedly"); });
  }

  void StrictTwoWayFields(StrictTwoWayFieldsCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("StrictTwoWayFields called unexpectedly"); });
  }

  void FlexibleTwoWay(FlexibleTwoWayCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("FlexibleTwoWay called unexpectedly"); });
  }

  void FlexibleTwoWayFields(FlexibleTwoWayFieldsCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("FlexibleTwoWayFields called unexpectedly"); });
  }

  void StrictTwoWayErr(StrictTwoWayErrCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("StrictTwoWayErr called unexpectedly"); });
  }

  void StrictTwoWayFieldsErr(StrictTwoWayFieldsErrCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("StrictTwoWayFieldsErr called unexpectedly"); });
  }

  void FlexibleTwoWayErr(FlexibleTwoWayErrCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("FlexibleTwoWayErr called unexpectedly"); });
  }

  void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("FlexibleTwoWayFieldsErr called unexpectedly"); });
  }

  void handle_unknown_method(
      ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsDriverProtocol> metadata,
      ::fidl::UnknownMethodCompleter::Sync& completer) override {
    add_server_assertion([]() { ADD_FAILURE("Unexpected flexible unknown interaction"); });
  }

 public:
  ~UnknownInteractionsServerBase() override = default;
};

class UnknownInteractions : public ::zxtest::Test {
 protected:
  void SetUp() override {
    auto dispatcher = fdf::Dispatcher::Create(
        FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
        [dispatcher_shutdown = dispatcher_shutdown_](fdf_dispatcher_t* dispatcher) {
          dispatcher_shutdown->Signal();
        });
    ASSERT_TRUE(dispatcher.is_ok());
    dispatcher_ = std::move(dispatcher.value());

    auto endpoints = fdf::CreateEndpoints<test::UnknownInteractionsDriverProtocol>();
    ASSERT_OK(endpoints.status_value());
    client_end_ = std::move(endpoints->client);
    ASSERT_TRUE(client_end_.is_valid());
    server_end_ = std::move(endpoints->server);
    ASSERT_TRUE(server_end_.is_valid());
  }

  void TearDown() override {
    if (client_) {
      libsync::Completion unbound;
      async::PostTask(AsyncDispatcher(), [&unbound, &client = client_.value()]() {
        client.AsyncTeardown();
        unbound.Signal();
      });
      ASSERT_OK(unbound.Wait());
    }

    if (unbind_) {
      server_->run_server_assertions();
      libsync::Completion unbound;
      async::PostTask(AsyncDispatcher(),
                      [&unbound, &unbind = unbind_.value(), &server = server_]() {
                        unbind();
                        { auto s = std::move(server); }
                        unbound.Signal();
                      });
      ASSERT_OK(unbound.Wait());
    }

    dispatcher_->ShutdownAsync();
    ASSERT_OK(dispatcher_shutdown_->Wait());
  }

  template <typename ServerImpl>
  ServerImpl* BindServer(std::unique_ptr<ServerImpl> impl) {
    EXPECT_EQ(nullptr, server_);
    auto server_end = fdf::ServerEnd<typename ServerImpl::_EnclosingProtocol>(TakeServerChannel());
    auto* server = impl.get();
    server_ = std::move(impl);
    auto* dispatcher = Dispatcher();

    libsync::Completion bound;
    EXPECT_OK(async::PostTask(
        AsyncDispatcher(), [&bound, &server_end, server, &unbind = this->unbind_, dispatcher]() {
          auto binding_ref = fdf::BindServer(dispatcher, std::move(server_end), server);
          unbind.emplace([binding_ref]() mutable { binding_ref.Unbind(); });
          bound.Signal();
        }));

    EXPECT_OK(bound.Wait());

    return server;
  }

  // Take the server end channel directly. May only be called once. Once this
  // has been called, |TakeServerEnd| is no longer allowed.
  fdf::Channel TakeServerChannel() {
    EXPECT_TRUE(server_end_.is_valid());
    return server_end_.TakeHandle();
  }

  // Take the client channel directly. May only be called once. Once this has
  // been called, |AsyncClient| is no longer allowed.
  fdf::Channel TakeClientChannel() {
    EXPECT_TRUE(client_end_.is_valid());
    return client_end_.TakeHandle();
  }

  // Retrieve the client to use for async FDF calls. May be called more than
  // once. If the client has not been used yet, bind it. Once this has been
  // called, |TakeClientChannel| is no longer allowed.
  fdf::SharedClient<test::UnknownInteractionsDriverProtocol>& AsyncClient() {
    if (client_) {
      return client_.value();
    }
    EXPECT_TRUE(client_end_.is_valid());
    auto client_end = std::move(client_end_);
    auto& client = client_.emplace();
    auto* dispatcher = Dispatcher();

    libsync::Completion bound;
    EXPECT_OK(async::PostTask(AsyncDispatcher(), [&bound, &client_end, &client, dispatcher]() {
      client.Bind(std::move(client_end), dispatcher);
      bound.Signal();
    }));
    EXPECT_OK(bound.Wait());
    return client;
  }

  // Run code using the AsyncClient on the dispatcher thread and wait for it to complete.
  void WithAsyncClientBlocking(
      fit::function<void(fdf::SharedClient<test::UnknownInteractionsDriverProtocol>&)>&& func) {
    auto& client = AsyncClient();
    libsync::Completion done;
    async::PostTask(AsyncDispatcher(), [&done, &client, func = std::move(func)]() {
      func(client);
      done.Signal();
    });
    ASSERT_OK(done.Wait());
  }

  // Run code using the AsyncClient on the dispatcher thread. Caller is
  // responsible for synchronizing and making sure the func doesn't outlive any
  // borrowed references.
  void WithAsyncClient(
      fit::function<void(fdf::SharedClient<test::UnknownInteractionsDriverProtocol>&)>&& func) {
    auto client = AsyncClient().Clone();
    async::PostTask(AsyncDispatcher(), [client = std::move(client),
                                        func = std::move(func)]() mutable { func(client); });
  }

  fdf_dispatcher_t* Dispatcher() { return dispatcher_.value().get(); }

  async_dispatcher_t* AsyncDispatcher() { return dispatcher_.value().async_dispatcher(); }

 private:
  fidl_driver_testing::ScopedFakeDriver driver;
  std::shared_ptr<libsync::Completion> dispatcher_shutdown_ =
      std::make_shared<libsync::Completion>();
  std::optional<fdf::Dispatcher> dispatcher_;

  fdf::ClientEnd<test::UnknownInteractionsDriverProtocol> client_end_;
  // If an AsyncClient is bound, it will be stored here.
  std::optional<fdf::SharedClient<test::UnknownInteractionsDriverProtocol>> client_;

  fdf::ServerEnd<test::UnknownInteractionsDriverProtocol> server_end_;
  // If a server impl is bound, it will be stored here.
  std::unique_ptr<TestServerBase> server_;
  std::optional<fit::function<void()>> unbind_;
};

template <size_t N>
std::array<uint8_t, N - sizeof(zx_txid_t)> ExcludeTxid(std::array<uint8_t, N> buf) {
  std::array<uint8_t, N - sizeof(zx_txid_t)> without_txid;
  std::memcpy(without_txid.data(), buf.data() + sizeof(zx_txid_t), without_txid.size());
  return without_txid;
}

// Helper for receiving raw data from a channel.
template <uint32_t N>
struct ReadResult {
  zx_status_t status;
  // Bytes from the read.
  std::array<uint8_t, N> buf;

  ReadResult() = delete;
  static ReadResult<N> ReadFromChannel(const fdf::Channel& channel, fdf_dispatcher_t* dispatcher) {
    ReadResult<N> result(channel, dispatcher);
    return result;
  }

  // Get the contents of the buffer excluding the transaction ID. This is used
  // to assert on the contents of a message generated by the bindings, where the
  // bindings will pick a txid that isn't known ahead of time.
  std::array<uint8_t, N - sizeof(zx_txid_t)> buf_excluding_txid() { return ExcludeTxid(buf); }

  // Get the transaction id portion of the buffer.
  zx_txid_t txid() {
    zx_txid_t value;
    std::memcpy(&value, buf.data(), sizeof(zx_txid_t));
    return value;
  }

 protected:
  // Constructs a |ReadResult| by waiting for a message to be available on the
  // given |fdf::Channel|, then reading it, asserting that the read is
  // successful and has the expected size and no handles, and stores the read
  // bytes in buf.
  explicit ReadResult(const fdf::Channel& channel, fdf_dispatcher_t* dispatcher) {
    libsync::Completion read_completion;
    auto channel_read = std::make_unique<fdf::ChannelRead>(
        channel.get(), 0,
        [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                           zx_status_t status) { read_completion.Signal(); });
    status = channel_read->Begin(dispatcher);
    if (status != ZX_OK)
      return;
    status = read_completion.Wait();
    if (status != ZX_OK)
      return;

    auto result = channel.Read(0);
    status = result.status_value();
    if (status != ZX_OK)
      return;
    ASSERT_EQ(N, result->num_bytes);
    ASSERT_EQ(0, result->handles.size());
    memcpy(buf.data(), result->data, N);
  }
};

template <size_t N>
void ChannelWrite(const fdf::Channel& channel, std::array<uint8_t, N> bytes) {
  fdf::Arena arena('TEST');
  auto data = arena.Allocate(N);
  memcpy(data, bytes.data(), N);

  ASSERT_TRUE(
      channel.Write(0, arena, data, static_cast<uint32_t>(N), cpp20::span<zx_handle_t>()).is_ok());
}

template <uint32_t N>
struct TwoWayServerRequest : public ReadResult<N> {
  TwoWayServerRequest() = delete;

  static TwoWayServerRequest<N> ReadFromChannel(const fdf::Channel& channel,
                                                fdf_dispatcher_t* dispatcher) {
    TwoWayServerRequest<N> result(channel, dispatcher);
    return result;
  }

  // Helper to send a reply to the read as a two-way message.
  // Copies the txid (first four) bytes from |buf| into |reply_bytes| and sends
  // the result on the channel, storing the status in |reply_status|.
  template <size_t M>
  void reply(const fdf::Channel& channel, std::array<uint8_t, M> reply_bytes) {
    zx_txid_t txid = this->txid();
    std::memcpy(reply_bytes.data(), &txid, sizeof(zx_txid_t));

    ChannelWrite(channel, reply_bytes);
  }

 protected:
  using ReadResult<N>::ReadResult;
};

template <typename T>
struct ResponseCompleter {
 public:
  void Signal(T&& value) const {
    inner_->response = std::move(value);
    inner_->completion.Signal();
  }

  zx::result<T> WaitForResponse() const {
    auto status = inner_->completion.Wait();
    if (status != ZX_OK) {
      return fit::error(status);
    }
    return fit::ok(std::move(inner_->response.value()));
  }

 private:
  struct Inner {
    std::optional<T> response;
    libsync::Completion completion;
  };
  std::shared_ptr<Inner> inner_ = std::make_shared<Inner>();
};

enum class ResultUnionTag : fidl_union_tag_t {
  kSuccess = 1,
  kApplicationError = 2,
  kTransportError = 3,
};

class InlineValue : public std::array<uint8_t, 4> {
 public:
  InlineValue(uint32_t value) { std::memcpy(data(), &value, sizeof(value)); }

  InlineValue(int32_t value) { std::memcpy(data(), &value, sizeof(value)); }
};

// Make an array representing a message with a transaction header and body.
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  using Traits = typename ::fidl::TypeTraits<::fidl::WireResponse<FidlMethod>>;
  static_assert(sizeof(fidl_xunion_v2_t) == Traits::kPrimarySize);
  static_assert(Traits::kMaxOutOfLine == 0);
  fidl_message_header_t header{
      .txid = txid,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = fidl::internal::WireOrdinal<FidlMethod>::value,
  };
  fidl_xunion_v2_t body{
      .tag = static_cast<std::underlying_type_t<ResultUnionTag>>(result_union_tag),
      .envelope =
          {
              .num_handles = 0,
              .flags = 1,
          },
  };
  std::memcpy(body.envelope.inline_value, inline_value.data(), sizeof(body.envelope.inline_value));
  std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> result;
  std::memcpy(result.data(), &header, sizeof(fidl_message_header_t));
  std::memcpy(result.data() + sizeof(fidl_message_header_t), &body, sizeof(fidl_xunion_v2_t));
  return result;
}

// Make an array representing a message with a transaction header and body.
// This version is for tests which don't care about the txid or want a zero
// txid (e.g. one way interactions).
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  return MakeMessage<FidlMethod>(0, dynamic_flags, result_union_tag, inline_value);
}

// Make an array representing a message with just a transaction header.
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags) {
  fidl_message_header_t header{
      .txid = txid,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = fidl::internal::WireOrdinal<FidlMethod>::value,
  };
  std::array<uint8_t, sizeof(fidl_message_header_t)> result;
  std::memcpy(result.data(), &header, sizeof(fidl_message_header_t));
  return result;
}

// Make an array representing a message with just a transaction header.
// This version is for tests which don't care about the txid or want a zero
// txid (e.g. one way interactions).
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags) {
  return MakeMessage<FidlMethod>(0, dynamic_flags);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// Client Side Tests
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//// One-Way Methods - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, OneWayStrictAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fit::result<fidl::Error>> response;
  WithAsyncClientBlocking([response](auto& client) { response.Signal(client->StrictOneWay()); });
  auto result = response.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictOneWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fit::result<fidl::Error>> response;
  WithAsyncClientBlocking([response](auto& client) { response.Signal(client->FlexibleOneWay()); });
  auto result = response.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_EQ(expected, received.buf);
}

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::StrictTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->StrictTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->StrictTwoWayErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOtherTransportError) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOkTransportErr) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendErrorVariant) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayFields().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_ok());
  EXPECT_EQ(32, result.value().value().some_field());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayFields().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, result.value().error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendOtherTransportError) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.value().error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendErrorVariant) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_domain_error());
  EXPECT_EQ(0x100, result.value().error_value().domain_error());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayFieldsErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_ok());
  EXPECT_EQ(32, result.value().value().some_field());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayFieldsErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, result.value().error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendErrorVariant) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>>
      response_completer;
  WithAsyncClient([response_completer](auto& client) {
    client->FlexibleTwoWayFieldsErr().Then(
        [response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  ASSERT_OK(received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_domain_error());
  EXPECT_EQ(0x100, result.value().error_value().domain_error());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// Server Side Tests
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Server
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, StrictTwoWayResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWay(StrictTwoWayCompleter::Sync& completer) override { completer.Reply(); }
  };
  BindServer(std::make_unique<Server>());

  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, StrictTwoWayResponseMismatchedStrictness) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWay(StrictTwoWayCompleter::Sync& completer) override { completer.Reply(); }
  };
  BindServer(std::make_unique<Server>());

  // Server is not supposed to validate the flexible flag for known methods.
  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, StrictTwoWayErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWayErr(StrictTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::ok());
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWay(FlexibleTwoWayCompleter::Sync& completer) override { completer.Reply(); }
  };
  BindServer(std::make_unique<Server>());

  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayResponseMismatchedStrictness) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWay(FlexibleTwoWayCompleter::Sync& completer) override { completer.Reply(); }
  };
  BindServer(std::make_unique<Server>());

  // Server is not supposed to validate the flexible flag for known methods.
  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFields(FlexibleTwoWayFieldsCompleter::Sync& completer) override {
      completer.Reply({{.some_field = 42}});
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
          0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFields>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 42);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayErr(FlexibleTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::ok());
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayErrResponseError) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayErr(FlexibleTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::error(3203));
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError,
      3203);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
      completer.Reply(
          fit::ok(::test::UnknownInteractionsDriverProtocolFlexibleTwoWayFieldsErrResponse(
              {.some_field = 42})));
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
          0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 42);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsErrResponseError) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
      completer.Reply(fit::error(3203));
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
          0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  ASSERT_OK(received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsDriverProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError,
      3203);
  EXPECT_EQ(expected, received.buf);
}

///////////////////////////////////////////////////////////////////////////////
//// Unknown messages - Server
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, UnknownStrictOneWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
   public:
    libsync::Completion ran_unknown_interaction_handler;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion([metadata]() {
        EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
        EXPECT_EQ(::fidl::UnknownMethodType::kOneWay, metadata.unknown_interaction_type);
      });

      ran_unknown_interaction_handler.Signal();
    }
  };
  auto* server = BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  ASSERT_OK(server->ran_unknown_interaction_handler.Wait());

  // Write again to test that the channel is still open.
  ChannelWrite(client, client_request);
}

TEST_F(UnknownInteractions, UnknownStrictTwoWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
   public:
    libsync::Completion ran_unknown_interaction_handler;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion([metadata]() {
        EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
        EXPECT_EQ(::fidl::UnknownMethodType::kTwoWay, metadata.unknown_interaction_type);
      });

      ran_unknown_interaction_handler.Signal();
    }
  };
  auto* server = BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  ASSERT_OK(server->ran_unknown_interaction_handler.Wait());

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod,
                                     ResultUnionTag::kTransportError, ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, UnknownStrictOneWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsAjarDriverProtocol>,
                 public TestServerBase {
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion([]() { ADD_FAILURE("Unexpected flexible unknown interaction"); });
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWayAjarPotocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsAjarDriverProtocol>,
                 public TestServerBase {
   public:
    libsync::Completion ran_unknown_interaction_handler;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion(
          [metadata]() { EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal); });
      ran_unknown_interaction_handler.Signal();
    }
  };
  auto* server = BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  ASSERT_OK(server->ran_unknown_interaction_handler.Wait());

  // Write again to test that the channel is still open.
  ChannelWrite(client, client_request);
}

TEST_F(UnknownInteractions, UnknownStrictTwoWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsAjarDriverProtocol>,
                 public TestServerBase {
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion([]() { ADD_FAILURE("Unexpected flexible unknown interaction"); });
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsAjarDriverProtocol>,
                 public TestServerBase {
   public:
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarDriverProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      add_server_assertion([]() { ADD_FAILURE("Unexpected flexible unknown interaction"); });
    }
  };
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownStrictOneWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsClosedDriverProtocol>,
                 public TestServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWayClosedPotocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsClosedDriverProtocol>,
                 public TestServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownStrictTwoWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsClosedDriverProtocol>,
                 public TestServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<16>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fdf::Server<::test::UnknownInteractionsClosedDriverProtocol>,
                 public TestServerBase {};
  BindServer(std::make_unique<Server>());

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ChannelWrite(client, client_request);

  auto received = ReadResult<32>::ReadFromChannel(client, Dispatcher());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}
}  // namespace
