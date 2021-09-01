// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.v2integration.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/message.h>

#include <iterator>
#include <thread>

#include <gtest/gtest.h>

typedef struct {
  uint64_t ordinal;
  uint32_t value;
  uint16_t num_handles;
  uint16_t flags;
} union_t;

using TestProtocol = ::llcpptest_v2integration_test::TestProtocol;

void SingleResponseServer(zx::channel ch) {
  ZX_ASSERT(ZX_OK == ch.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));

  std::unique_ptr<uint8_t[]> bytes_in = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  ZX_ASSERT(ZX_OK ==
            ch.read_etc(0, bytes_in.get(), nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0, nullptr, nullptr));
  fidl_message_header_t header_in;
  memcpy(&header_in, bytes_in.get(), sizeof(header_in));

  fidl_message_header_t header_out = {
      .txid = header_in.txid,
      .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2, 0, 0},
      .magic_number = header_in.magic_number,
      .ordinal = header_in.ordinal,
  };
  union_t payload_out = {
      .ordinal = 1,
      .value = 123,
      .num_handles = 0,
      .flags = 1,  // 1 == inlined
  };
  uint8_t bytes_out[sizeof(header_out) + sizeof(payload_out)] = {};
  memcpy(bytes_out, &header_out, sizeof(header_out));
  memcpy(bytes_out + sizeof(header_out), &payload_out, sizeof(payload_out));
  ZX_ASSERT(ZX_OK == ch.write_etc(0, bytes_out, std::size(bytes_out), nullptr, 0));
}

// Tests an LLCPP sync client where the server returns a V2 message.
TEST(V2Integration, SyncCallResponseDecode) {
  zx::channel ch1, ch2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));

  std::thread server_thread([&ch2]() { SingleResponseServer(std::move(ch2)); });

  fidl::ClientEnd<TestProtocol> client_end(std::move(ch1));
  fidl::WireSyncClient<TestProtocol> client(std::move(client_end));

  auto result = client.MethodWithResponse();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(123u, result->u.v());

  server_thread.join();
}

// Tests an LLCPP async client where the server returns a V2 message.
TEST(V2Integration, AsyncCallResponseDecode) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread());

  zx::channel ch1, ch2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));

  std::thread server_thread([&ch2]() { SingleResponseServer(std::move(ch2)); });

  fidl::ClientEnd<TestProtocol> client_end(std::move(ch1));
  fidl::WireSharedClient<TestProtocol> client(std::move(client_end), loop.dispatcher());

  sync_completion_t done;
  auto result = client->MethodWithResponse(
      [&done](fidl::WireResponse<TestProtocol::MethodWithResponse>* response) {
        ASSERT_EQ(123u, response->u.v());
        sync_completion_signal(&done);
      });
  ASSERT_TRUE(result.ok());

  ASSERT_EQ(ZX_OK, sync_completion_wait(&done, ZX_TIME_INFINITE));
  server_thread.join();
}

// Tests an LLCPP server which decodes a V2 request.
TEST(V2Integration, ServerRequestDecode) {
  class Server : public fidl::WireServer<TestProtocol> {
   public:
    Server(sync_completion_t* done) : done_(done) {}

    void MethodWithRequest(MethodWithRequestRequestView request,
                           MethodWithRequestCompleter::Sync& completer) override {
      ASSERT_EQ(123u, request->u.v());
      sync_completion_signal(done_);
    }

    void MethodWithResponse(MethodWithResponseRequestView request,
                            MethodWithResponseCompleter::Sync& completer) override {
      ZX_PANIC("Not used in this test");
    }

   private:
    sync_completion_t* done_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread());

  zx::channel ch1, ch2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));

  sync_completion_t done;
  fidl::ServerEnd<TestProtocol> server_end(std::move(ch2));
  fidl::BindServer(loop.dispatcher(), std::move(server_end), std::make_unique<Server>(&done));

  fidl_message_header_t header = {
      .txid = 100,
      .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2, 0, 0},
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = 8068486508660569159ull,
  };
  union_t payload = {
      .ordinal = 1,
      .value = 123,
      .num_handles = 0,
      .flags = 1,  // 1 == inlined
  };
  uint8_t bytes[sizeof(header) + sizeof(payload)] = {};
  memcpy(bytes, &header, sizeof(header));
  memcpy(bytes + sizeof(header), &payload, sizeof(payload));
  ASSERT_EQ(ZX_OK, ch1.write_etc(0, bytes, std::size(bytes), nullptr, 0));

  ASSERT_EQ(ZX_OK, sync_completion_wait(&done, ZX_TIME_INFINITE));
}
