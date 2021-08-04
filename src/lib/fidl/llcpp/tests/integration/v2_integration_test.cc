// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message.h>

#include <iterator>
#include <thread>

#include <gtest/gtest.h>
#include <llcpptest/v2integration/test/llcpp/fidl.h>

typedef struct {
  uint64_t ordinal;
  uint32_t value;
  uint16_t num_handles;
  uint16_t flags;
} union_t;

// Tests an LLCPP sync client where the server returns a V2 message.
TEST(V2Integration, SyncCallResponseDecode) {
  zx::channel ch1, ch2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));

  std::thread server_thread([ch2 = std::move(ch2)]() {
    ZX_ASSERT(ZX_OK == ch2.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));

    std::unique_ptr<uint8_t[]> bytes_in = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
    ZX_ASSERT(ZX_OK == ch2.read_etc(0, bytes_in.get(), nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                    nullptr, nullptr));
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
    ZX_ASSERT(ZX_OK == ch2.write_etc(0, bytes_out, std::size(bytes_out), nullptr, 0));
  });

  fidl::ClientEnd<::llcpptest_v2integration_test::TestProtocol> client_end(std::move(ch1));
  fidl::WireSyncClient<::llcpptest_v2integration_test::TestProtocol> client(std::move(client_end));

  auto result = client.MethodWithResponse();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(123u, result->u.v());

  server_thread.join();
}
