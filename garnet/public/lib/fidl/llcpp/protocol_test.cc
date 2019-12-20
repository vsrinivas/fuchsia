// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <llcpptest/protocol/test/llcpp/fidl.h>

#include "gtest/gtest.h"

namespace test = ::llcpp::llcpptest::protocol::test;

namespace {
zx_status_t kErrorStatus = 271;
}  // namespace

class ErrorServer : public test::ErrorMethods::Interface {
 public:
  void NoArgsPrimitiveError(bool should_error,
                            NoArgsPrimitiveErrorCompleter::Sync completer) override {
    if (should_error) {
      return completer.ReplyError(kErrorStatus);
    } else {
      return completer.ReplySuccess();
    }
  }
  void ManyArgsCustomError(bool should_error,
                           ManyArgsCustomErrorCompleter::Sync completer) override {
    if (should_error) {
      return completer.ReplyError(test::MyError::REALLY_BAD_ERROR);
    } else {
      return completer.ReplySuccess(1, 2, 3);
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
    fidl::Bind(loop_->dispatcher(), std::move(server_end), server_.get());
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
  virtual void Frob(::fidl::StringView value, FrobCompleter::Sync completer) override {}

  virtual void Grob(::fidl::StringView value, GrobCompleter::Sync completer) override {
    completer.Reply(value);
  }
};

TEST(MagicNumberTest, RequestWrite) {
  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  std::string s = "hi";
  test::Frobinator::Call::Frob(zx::unowned_channel(h1), fidl::StringView(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_msg_t msg = {
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
  test::Frobinator::SendHrobEvent(zx::unowned_channel(h1), fidl::StringView(s));
  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl_msg_t msg = {
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
  fidl::Bind(loop.dispatcher(), std::move(h2), &server);

  fidl::Buffer<test::Frobinator::GrobRequest> request;
  fidl::Buffer<test::Frobinator::GrobResponse> response;
  auto result = test::Frobinator::Call::Grob(zx::unowned_channel(h1), request.view(),
                                             fidl::StringView(s), response.view());
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
  test::Frobinator::HrobResponse _response = {};
  test::Frobinator::SetTransactionHeaderFor::HrobResponse(
      fidl::DecodedMessage<test::Frobinator::HrobResponse>(fidl::BytePart(
          reinterpret_cast<uint8_t*>(&_response), test::Frobinator::HrobResponse::PrimarySize,
          test::Frobinator::HrobResponse::PrimarySize)));
  // Set an incompatible magic number
  reinterpret_cast<fidl_message_header_t*>(&_response)->magic_number = 0;
  _response.value = fidl::StringView(s);
  auto linearize_result = fidl::Linearize(&_response, fidl::BytePart(write_bytes, kWriteAllocSize));
  ASSERT_EQ(fidl::Write(zx::unowned_channel(h1), std::move(linearize_result.message)), ZX_OK);

  test::Frobinator::EventHandlers handlers;
  handlers.hrob = [&](fidl::StringView value) -> zx_status_t {
    EXPECT_TRUE(false);
    return ZX_OK;
  };
  handlers.unknown = [&]() -> zx_status_t {
    EXPECT_TRUE(false);
    return ZX_OK;
  };

  ASSERT_EQ(test::Frobinator::Call::HandleEvents(zx::unowned_channel(h2), std::move(handlers)),
            ZX_ERR_PROTOCOL_NOT_SUPPORTED);
}
