// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <cstdint>

#include <zxtest/zxtest.h>

namespace test = ::llcpptest_protocol_test;

namespace {
zx_status_t kErrorStatus = 271;

template <typename T>
uint32_t GetHandleCount(zx::unowned<T> h) {
  zx_info_handle_count_t info = {};
  auto status = h->get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return info.handle_count;
}

}  // namespace

class ErrorServer : public fidl::WireServer<test::ErrorMethods> {
 public:
  void NoArgsPrimitiveError(NoArgsPrimitiveErrorRequestView request,
                            NoArgsPrimitiveErrorCompleter::Sync& completer) override {
    if (request->should_error) {
      completer.ReplyError(kErrorStatus);
    } else {
      completer.ReplySuccess();
    }
  }
  void ManyArgsCustomError(ManyArgsCustomErrorRequestView request,
                           ManyArgsCustomErrorCompleter::Sync& completer) override {
    if (request->should_error) {
      completer.ReplyError(test::wire::MyError::kReallyBadError);
    } else {
      completer.ReplySuccess(1, 2, 3);
    }
  }
};

class ResultTest : public ::zxtest::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_result_server"), ZX_OK);

    auto endpoints = fidl::CreateEndpoints<test::ErrorMethods>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    auto [client_end, server_end] = std::move(endpoints.value());
    client_end_ = std::move(client_end);

    server_ = std::make_unique<ErrorServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  fidl::WireSyncClient<test::ErrorMethods> TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::ErrorMethods>(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<ErrorServer> server_;
  fidl::ClientEnd<test::ErrorMethods> client_end_;
};

TEST_F(ResultTest, OwnedPrimitiveError) {
  auto client = TakeClient();
  auto resp = client->NoArgsPrimitiveError(true);
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), kErrorStatus);
}

TEST_F(ResultTest, OwnedCustomError) {
  auto client = TakeClient();
  auto resp = client->ManyArgsCustomError(true);
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), test::wire::MyError::kReallyBadError);
}

TEST_F(ResultTest, OwnedSuccessNoArgs) {
  auto client = TakeClient();
  auto resp = client->NoArgsPrimitiveError(false);
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_response());
}

TEST_F(ResultTest, OwnedSuccessManyArgs) {
  auto client = TakeClient();
  auto resp = client->ManyArgsCustomError(false);
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_response());
  const auto& success = resp->result.response();
  ASSERT_EQ(success.a, 1);
  ASSERT_EQ(success.b, 2);
  ASSERT_EQ(success.c, 3);
}

class FrobinatorImpl : public fidl::WireServer<test::Frobinator> {
 public:
  virtual void Frob(FrobRequestView request, FrobCompleter::Sync& completer) override {}

  virtual void Grob(GrobRequestView request, GrobCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }
};

TEST(MagicNumberTest, RequestWrite) {
  auto endpoints = fidl::CreateEndpoints<test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [local, remote] = std::move(*endpoints);
  std::string s = "hi";
  fidl::WireCall(local)->Frob(fidl::StringView::FromExternal(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .num_bytes = 0u,
      .num_handles = 0u,
  };
  auto status =
      remote.channel().read_etc(0, bytes, handle_infos, ZX_CHANNEL_MAX_MSG_BYTES,
                                ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t));

  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial);
}

TEST(MagicNumberTest, EventWrite) {
  auto endpoints = fidl::CreateEndpoints<test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  std::string s = "hi";
  fidl::WireSendEvent(endpoints->server)->Hrob(fidl::StringView::FromExternal(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .num_bytes = 0u,
      .num_handles = 0u,
  };
  auto status = endpoints->client.channel().read_etc(
      0, bytes, handle_infos, ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes,
      &msg.num_handles);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t));

  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial);
}

TEST(MagicNumberTest, ResponseWrite) {
  auto loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread("test_llcpp_result_server"), ZX_OK);

  auto endpoints = fidl::CreateEndpoints<test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  std::string s = "hi";

  FrobinatorImpl server;
  fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server);

  fidl::SyncClientBuffer<test::Frobinator::Grob> fidl_buffer;
  auto result = WireCall(endpoints->client)
                    .buffer(fidl_buffer.view())
                    ->Grob(fidl::StringView::FromExternal(s));
  ASSERT_OK(result.status());
  uint8_t* body_bytes = reinterpret_cast<uint8_t*>(result.Unwrap());
  auto resp = reinterpret_cast<fidl::internal::TransactionalResponse<test::Frobinator::Grob>*>(
      body_bytes - sizeof(fidl_message_header_t));
  ASSERT_EQ(resp->header.magic_number, kFidlWireFormatMagicNumberInitial);
}

// Send an event with an incompatible magic number and check that the event
// handler returns ZX_ERR_PROTOCOL_NOT_SUPPORTED
TEST(MagicNumberTest, EventRead) {
  auto endpoints = fidl::CreateEndpoints<test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [local, remote] = std::move(*endpoints);
  std::string s = "foo";
  fidl::internal::TransactionalEvent<test::Frobinator::Hrob> _response(
      fidl::StringView::FromExternal(s));
  // Set an incompatible magic number
  _response.header.magic_number = 0;
  fidl::unstable::OwnedEncodedMessage<fidl::WireEvent<test::Frobinator::Hrob>> encoded(
      &_response.body);
  encoded.Write(remote.channel());
  ASSERT_OK(encoded.status());

  class EventHandler : public fidl::WireSyncEventHandler<test::Frobinator> {
   public:
    EventHandler() = default;

    void Hrob(fidl::WireEvent<test::Frobinator::Hrob>* event) override { EXPECT_TRUE(false); }

    zx_status_t Unknown() override {
      EXPECT_TRUE(false);
      return ZX_OK;
    }
  };

  EventHandler event_handler;
  ASSERT_EQ(event_handler.HandleOneEvent(local).status(), ZX_ERR_PROTOCOL_NOT_SUPPORTED);
}

TEST(SyncClientTest, DefaultInitializationError) {
  fidl::WireSyncClient<test::ErrorMethods> client;
  ASSERT_FALSE(client.is_valid());
  ASSERT_DEATH([&] { client->NoArgsPrimitiveError(false); });
}

TEST(EventSenderTest, SendEvent) {
  auto endpoints = fidl::CreateEndpoints<test::Frobinator>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [client_end, server_end] = std::move(endpoints.value());
  ASSERT_EQ(ZX_OK, fidl::WireSendEvent(server_end)->Hrob(fidl::StringView("foo")).status());

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  class EventHandler : public fidl::WireAsyncEventHandler<test::Frobinator> {
   public:
    EventHandler(async::Loop& loop) : loop_(loop) {}

    bool received() const { return received_; }

    void Hrob(fidl::WireEvent<test::Frobinator::Hrob>* event) override {
      ASSERT_EQ(std::string(event->value.data(), event->value.size()), std::string("foo"));
      received_ = true;
      loop_.Quit();
    }

   private:
    async::Loop& loop_;
    bool received_ = false;
  };

  auto event_handler = std::make_shared<EventHandler>(loop);
  fidl::WireSharedClient<test::Frobinator> client(std::move(client_end), loop.dispatcher(),
                                                  event_handler.get(),
                                                  fidl::ShareUntilTeardown(event_handler));

  loop.Run();
  ASSERT_TRUE(event_handler->received());
}

class HandleProviderServer : public fidl::WireServer<test::HandleProvider> {
 public:
  void GetHandle(GetHandleRequestView request, GetHandleCompleter::Sync& completer) override {
    test::wire::HandleStruct s;
    zx::event::create(0, &s.h);
    completer.Reply(std::move(s));
  }

  void GetHandleVector(GetHandleVectorRequestView request,
                       GetHandleVectorCompleter::Sync& completer) override {
    std::vector<test::wire::HandleStruct> v(request->count);
    for (auto& s : v) {
      zx::event::create(0, &s.h);
    }
    completer.Reply(fidl::VectorView<test::wire::HandleStruct>::FromExternal(v));
  }

  void GetHandleUnion(GetHandleUnionRequestView request,
                      GetHandleUnionCompleter::Sync& completer) override {
    zx::event h;
    zx::event::create(0, &h);
    test::wire::HandleUnionStruct s = {.u = test::wire::HandleUnion::WithH(std::move(h))};
    completer.Reply(std::move(s));
  }
};

class HandleTest : public ::zxtest::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_server"), ZX_OK);

    auto endpoints = fidl::CreateEndpoints<test::HandleProvider>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    client_end_ = std::move(endpoints->client);
    server_ = std::make_unique<HandleProviderServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(endpoints->server), server_.get());
  }

  fidl::WireSyncClient<test::HandleProvider> TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::HandleProvider>(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<HandleProviderServer> server_;
  fidl::ClientEnd<test::HandleProvider> client_end_;
};

TEST_F(HandleTest, HandleClosedAfterHandleStructMove) {
  auto client = TakeClient();
  auto result = client->GetHandle();

  ASSERT_OK(result.status());
  ASSERT_TRUE(result->value.h.is_valid());

  // Dupe the event so we can get the handle count after move.
  zx::event dupe;
  ASSERT_EQ(result->value.h.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe), ZX_OK);

  // A move of a struct holding a handle will move the handle from the result, resulting in a close
  { auto release = std::move(result->value); }  // ~HandleStruct

  // Only remaining handle should be the dupe.
  ASSERT_EQ(GetHandleCount(dupe.borrow()), 1u);
}

TEST_F(HandleTest, HandleClosedOnResultOfDestructorAfterVectorMove) {
  constexpr uint32_t kNumHandles = 2;

  auto client = TakeClient();
  std::vector<zx::event> dupes(kNumHandles);

  {
    auto result = client->GetHandleVector(kNumHandles);

    ASSERT_OK(result.status());
    ASSERT_EQ(result->value.count(), kNumHandles);

    for (uint32_t i = 0; i < kNumHandles; i++) {
      ASSERT_TRUE(result->value[i].h.is_valid());
      ASSERT_EQ(result->value[i].h.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupes[i]), ZX_OK);
    }

    // std::move of VectorView only moves pointers, not handles.
    // 1 handle in ResultOf + 1 handle in dupe = 2.
    for (auto& e : dupes) {
      ASSERT_EQ(GetHandleCount(e.borrow()), 2u);
    }
  }

  // Handle cleaned up after ResultOf destructor is called.
  // Remaining handle is the dupe.
  for (auto& e : dupes) {
    ASSERT_EQ(GetHandleCount(e.borrow()), 1u);
  }
}

TEST_F(HandleTest, HandleUnion) {
  auto client = TakeClient();
  auto result = client->GetHandleUnion();

  ASSERT_OK(result.status());
  ASSERT_TRUE(result->value.u.h().is_valid());

  // Dupe the event so we can get the handle count after move.
  zx::event dupe;
  ASSERT_EQ(result->value.u.h().duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe), ZX_OK);

  // Should have two dupes before releasing the original handle.
  ASSERT_EQ(GetHandleCount(dupe.borrow()), 2u);

  // A move of a struct holding a handle will move the handle from the result, resulting in a close
  { auto release = std::move(result->value); }  // ~HandleUnionStruct

  // Only remaining handle should be the dupe.
  ASSERT_EQ(GetHandleCount(dupe.borrow()), 1u);
}

class EmptyImpl : public fidl::WireServer<test::Empty> {
 public:
};

TEST(EmptyTest, EmptyProtocolHasBindableInterface) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<test::Empty>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);

  EmptyImpl server;
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &server);
}

// Test creating a typed channel endpoint pair.
TEST(Endpoints, CreateFromProtocol) {
  // `std::move` pattern
  {
    auto endpoints = fidl::CreateEndpoints<test::Empty>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    fidl::ClientEnd<test::Empty> client_end = std::move(endpoints->client);
    fidl::ServerEnd<test::Empty> server_end = std::move(endpoints->server);

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }

  // Destructuring pattern
  {
    auto endpoints = fidl::CreateEndpoints<test::Empty>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    auto [client_end, server_end] = std::move(endpoints.value());

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }
}

// Test creating a typed channel endpoint pair using the out-parameter
// overloads.
TEST(Endpoints, CreateFromProtocolOutParameterStyleClientRetained) {
  fidl::ClientEnd<test::Empty> client_end;
  auto server_end = fidl::CreateEndpoints(&client_end);
  ASSERT_OK(server_end.status_value());
  ASSERT_EQ(ZX_OK, server_end.status_value());

  ASSERT_TRUE(client_end.is_valid());
  ASSERT_TRUE(server_end->is_valid());
}

TEST(Endpoints, CreateFromProtocolOutParameterStyleServerRetained) {
  fidl::ServerEnd<test::Empty> server_end;
  auto client_end = fidl::CreateEndpoints(&server_end);
  ASSERT_OK(client_end.status_value());
  ASSERT_EQ(ZX_OK, client_end.status_value());

  ASSERT_TRUE(server_end.is_valid());
  ASSERT_TRUE(client_end->is_valid());
}
