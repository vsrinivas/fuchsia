// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <cstdint>

#include <gtest/gtest.h>
#include <llcpptest/protocol/test/llcpp/fidl.h>

namespace test = ::llcpp::llcpptest::protocol::test;

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

class ErrorServer : public test::ErrorMethods::Interface {
 public:
  void NoArgsPrimitiveError(bool should_error,
                            NoArgsPrimitiveErrorCompleter::Sync& completer) override {
    if (should_error) {
      completer.ReplyError(kErrorStatus);
    } else {
      completer.ReplySuccess();
    }
  }
  void ManyArgsCustomError(bool should_error,
                           ManyArgsCustomErrorCompleter::Sync& completer) override {
    if (should_error) {
      completer.ReplyError(test::MyError::REALLY_BAD_ERROR);
    } else {
      completer.ReplySuccess(1, 2, 3);
    }
  }
};

class ResultTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_result_server"), ZX_OK);

    zx::channel server_end;
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end), ZX_OK);
    server_ = std::make_unique<ErrorServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  test::ErrorMethods::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::ErrorMethods::SyncClient(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<ErrorServer> server_;
  zx::channel client_end_;
};

TEST_F(ResultTest, OwnedPrimitiveError) {
  auto client = TakeClient();
  auto resp = client.NoArgsPrimitiveError(true);
  ASSERT_TRUE(resp.ok()) << resp.error();
  ASSERT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), kErrorStatus);
}

TEST_F(ResultTest, OwnedCustomError) {
  auto client = TakeClient();
  auto resp = client.ManyArgsCustomError(true);
  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), test::MyError::REALLY_BAD_ERROR);
}

TEST_F(ResultTest, OwnedSuccessNoArgs) {
  auto client = TakeClient();
  auto resp = client.NoArgsPrimitiveError(false);
  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_response());
}

TEST_F(ResultTest, OwnedSuccessManyArgs) {
  auto client = TakeClient();
  auto resp = client.ManyArgsCustomError(false);
  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_response());
  const auto& success = resp->result.response();
  ASSERT_EQ(success.a, 1);
  ASSERT_EQ(success.b, 2);
  ASSERT_EQ(success.c, 3);
}

class FrobinatorImpl : public test::Frobinator::Interface {
 public:
  virtual void Frob(::fidl::StringView value, FrobCompleter::Sync& completer) override {}

  virtual void Grob(::fidl::StringView value, GrobCompleter::Sync& completer) override {
    completer.Reply(std::move(value));
  }
};

TEST(MagicNumberTest, RequestWrite) {
  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  std::string s = "hi";
  test::Frobinator::Call::Frob(zx::unowned_channel(h1), fidl::unowned_str(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0u,
      .num_handles = 0u,
  };
  auto status = zx_channel_read(h2.get(), 0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                                ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t));

  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial);
}

TEST(MagicNumberTest, EventWrite) {
  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  std::string s = "hi";
  test::Frobinator::SendHrobEvent(zx::unowned_channel(h1), fidl::unowned_str(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0u,
      .num_handles = 0u,
  };
  auto status = zx_channel_read(h2.get(), 0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                                ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t));

  auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial);
}

TEST(MagicNumberTest, ResponseWrite) {
  auto loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread("test_llcpp_result_server"), ZX_OK);

  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  std::string s = "hi";

  FrobinatorImpl server;
  fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(h2), &server);

  fidl::Buffer<test::Frobinator::GrobRequest> request;
  fidl::Buffer<test::Frobinator::GrobResponse> response;
  auto result = test::Frobinator::Call::Grob(zx::unowned_channel(h1), request.view(),
                                             fidl::unowned_str(s), response.view());
  ASSERT_TRUE(result.ok());
  auto hdr = reinterpret_cast<fidl_message_header_t*>(response.view().data());
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial);
}

// Send an event with an incompatible magic number and check that the event
// handler returns ZX_ERR_PROTOCOL_NOT_SUPPORTED
TEST(MagicNumberTest, EventRead) {
  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  std::string s = "foo";
  constexpr uint32_t kWriteAllocSize =
      fidl::internal::ClampedMessageSize<test::Frobinator::HrobResponse,
                                         fidl::MessageDirection::kSending>();
  std::unique_ptr<uint8_t[]> write_bytes_unique_ptr(new uint8_t[kWriteAllocSize]);
  uint8_t* write_bytes = write_bytes_unique_ptr.get();
  test::Frobinator::HrobResponse _response(fidl::unowned_str(s));
  // Set an incompatible magic number
  _response._hdr.magic_number = 0;
  auto encode_result =
      fidl::LinearizeAndEncode(&_response, fidl::BytePart(write_bytes, kWriteAllocSize));
  ASSERT_EQ(fidl::Write(zx::unowned_channel(h1), std::move(encode_result.message)), ZX_OK);

  test::Frobinator::EventHandlers handlers;
  handlers.hrob = [&](test::Frobinator::HrobResponse* message) {
    EXPECT_TRUE(false);
    return ZX_OK;
  };
  handlers.unknown = [&]() {
    EXPECT_TRUE(false);
    return ZX_OK;
  };

  ASSERT_EQ(test::Frobinator::Call::HandleEvents(zx::unowned_channel(h2), handlers).status(),
            ZX_ERR_PROTOCOL_NOT_SUPPORTED);
}

TEST(SyncClientTest, DefaultInitializationError) {
  test::ErrorMethods::SyncClient client;
  ASSERT_FALSE(client.channel().is_valid());

  auto resp = client.NoArgsPrimitiveError(false);
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, resp.status());
}

class HandleProviderServer : public test::HandleProvider::Interface {
 public:
  void GetHandle(GetHandleCompleter::Sync& completer) override {
    test::HandleStruct s;
    zx::event::create(0, &s.h);
    completer.Reply(std::move(s));
  }

  void GetHandleVector(uint32_t count, GetHandleVectorCompleter::Sync& completer) override {
    std::vector<test::HandleStruct> v(count);
    for (auto& s : v) {
      zx::event::create(0, &s.h);
    }
    completer.Reply(fidl::unowned_vec(v));
  }

  void GetHandleUnion(GetHandleUnionCompleter::Sync& completer) override {
    zx::event h;
    zx::event::create(0, &h);
    test::HandleUnionStruct s = {.u = test::HandleUnion::WithH(fidl::unowned_ptr(&h))};
    completer.Reply(std::move(s));
  }
};

class HandleTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_server"), ZX_OK);

    zx::channel server_end;
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end), ZX_OK);
    server_ = std::make_unique<HandleProviderServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  test::HandleProvider::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::HandleProvider::SyncClient(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<HandleProviderServer> server_;
  zx::channel client_end_;
};

TEST_F(HandleTest, HandleClosedAfterHandleStructMove) {
  auto client = TakeClient();
  auto result = client.GetHandle();

  ASSERT_TRUE(result.ok()) << result.error();
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
    auto result = client.GetHandleVector(kNumHandles);

    ASSERT_TRUE(result.ok()) << result.error();
    ASSERT_EQ(result->value.count(), kNumHandles);

    for (uint32_t i = 0; i < kNumHandles; i++) {
      ASSERT_TRUE(result->value[i].h.is_valid());
      ASSERT_EQ(result->value[i].h.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupes[i]), ZX_OK);
    }

    { auto release = std::move(result->value); }  // ~VectorView<HandleStruct>

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

TEST_F(HandleTest, HandleClosedOnResultOfDestructorAfterTrackingPtrMove) {
  auto client = TakeClient();
  zx::event dupe;

  {
    auto result = client.GetHandleUnion();

    ASSERT_TRUE(result.ok()) << result.error();
    ASSERT_TRUE(result->value.u.h().is_valid());
    ASSERT_EQ(result->value.u.h().duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe), ZX_OK);

    { auto release = std::move(result->value); }  // ~HandleUnion

    // std::move of tracking_ptr in union only moves pointers, not handles.
    // 1 handle in ResultOf + 1 handle in dupe = 2.
    ASSERT_EQ(GetHandleCount(dupe.borrow()), 2u);
  }

  // Handle cleaned up after ResultOf destructor is called.
  // Remaining handle is the dupe.
  ASSERT_EQ(GetHandleCount(dupe.borrow()), 1u);
}

class EmptyImpl : public test::Empty::Interface {
 public:
};

TEST(EmptyTest, EmptyProtocolHasBindableInterface) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel client_end, server_end;
  ASSERT_EQ(zx::channel::create(0, &client_end, &server_end), ZX_OK);

  EmptyImpl server;
  fidl::BindServer(loop.dispatcher(), std::move(server_end), &server);
}
