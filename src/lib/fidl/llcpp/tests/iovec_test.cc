// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/message.h>

#include <thread>

#include <fidl/llcpp/linearized/test/llcpp/fidl.h>
#include <gtest/gtest.h>

namespace fidl_linearized = ::llcpp::fidl::llcpp::linearized::test;

void RunTest(
    const std::function<void(fidl::OutgoingIovecMessage*, fidl_linearized::FullyLinearizedStruct*,
                             fidl_linearized::InnerStruct*)>& run_test_body) {
  fidl_linearized::InnerStruct inner = {.x = 1};
  fidl_linearized::FullyLinearizedStruct input{.ptr = fidl::unowned_ptr(&inner)};

  constexpr uint32_t kNumIovecs = 3;
  zx_channel_iovec_t iovecs[kNumIovecs];
  constexpr uint32_t kNumSubstitutions = 3;
  fidl_iovec_substitution_t substitutions[kNumSubstitutions];

  fidl::OutgoingIovecMessage iovec_message({
      .iovecs = iovecs,
      .iovecs_actual = 0,
      .iovecs_capacity = kNumIovecs,
      .substitutions = substitutions,
      .substitutions_actual = 0,
      .substitutions_capacity = kNumSubstitutions,
      .handles = nullptr,
      .handle_actual = 0,
      .handle_capacity = 0,
  });
  iovec_message.Encode(&input);
  ASSERT_EQ(ZX_OK, iovec_message.status()) << iovec_message.error();

  run_test_body(&iovec_message, &input, &inner);
}

template <uint32_t N>
uint32_t Linearize(fidl::OutgoingIovecMessage* iovec_message, uint8_t (&bytes)[N]) {
  uint32_t offset = 0;
  for (uint32_t i = 0; i < iovec_message->iovec_actual(); i++) {
    zx_channel_iovec_t iovec = iovec_message->iovecs()[i];
    ZX_ASSERT(offset + iovec.capacity <= N);
    memcpy(&bytes[offset], iovec.buffer, iovec.capacity);
    offset += iovec.capacity;
  }
  return offset;
}

TEST(Iovec, Encode) {
  RunTest([](fidl::OutgoingIovecMessage* iovec_message,
             fidl_linearized::FullyLinearizedStruct* input, fidl_linearized::InnerStruct* inner) {
    ASSERT_EQ(3u, iovec_message->iovec_actual());

    EXPECT_EQ(input, iovec_message->iovecs()[0].buffer);
    EXPECT_EQ(sizeof(*input), iovec_message->iovecs()[0].capacity);
    EXPECT_EQ(0u, iovec_message->iovecs()[0].reserved);

    EXPECT_EQ(inner, iovec_message->iovecs()[1].buffer);
    EXPECT_EQ(sizeof(*inner), iovec_message->iovecs()[1].capacity);
    EXPECT_EQ(0u, iovec_message->iovecs()[1].reserved);

    static_assert(std::is_same<decltype(inner->x), int32_t>::value);
    EXPECT_EQ(0, *reinterpret_cast<const int32_t*>(iovec_message->iovecs()[2].buffer));
    EXPECT_EQ(8 - (sizeof(*inner)) % 8, iovec_message->iovecs()[2].capacity);
    EXPECT_EQ(0u, iovec_message->iovecs()[2].reserved);
  });
}

TEST(Iovec, Write) {
  RunTest([](fidl::OutgoingIovecMessage* iovec_message,
             fidl_linearized::FullyLinearizedStruct* input, fidl_linearized::InnerStruct* inner) {
    zx::channel ch1, ch2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));
    iovec_message->Write(ch1);
    ASSERT_EQ(ZX_OK, iovec_message->status()) << iovec_message->error();

    uint8_t expected_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t expected_num_bytes = Linearize(iovec_message, expected_bytes);

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    ch2.read(0, bytes, nullptr, sizeof(bytes), 0, &actual_bytes, &actual_handles);

    EXPECT_EQ(expected_num_bytes, actual_bytes);
    EXPECT_EQ(0u, actual_handles);
    EXPECT_EQ(0, memcmp(expected_bytes, bytes, expected_num_bytes));
  });
}

TEST(Iovec, Call) {
  RunTest([](fidl::OutgoingIovecMessage* iovec_message,
             fidl_linearized::FullyLinearizedStruct* input, fidl_linearized::InnerStruct* inner) {
    uint8_t expected_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t expected_num_bytes = Linearize(iovec_message, expected_bytes);

    zx::channel ch1, ch2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &ch1, &ch2));

    std::thread thread([&ch2, &expected_num_bytes]() {
      zx_signals_t signals;
      zx_status_t status = ch2.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                        zx::time::infinite(), &signals);
      ASSERT_EQ(ZX_OK, status);
      ASSERT_TRUE(signals & ZX_CHANNEL_READABLE);

      uint8_t buf[ZX_CHANNEL_MAX_MSG_BYTES];
      uint32_t actual_bytes;
      uint32_t actual_handles;
      ASSERT_EQ(ZX_OK, ch2.read(0, buf, nullptr, sizeof(buf), 0, &actual_bytes, &actual_handles));

      ASSERT_EQ(expected_num_bytes, actual_bytes);
      ASSERT_EQ(0u, actual_handles);

      ASSERT_EQ(ZX_OK, ch2.write(0, buf, actual_bytes, nullptr, 0));
    });

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    iovec_message->Call<fidl_linearized::FullyLinearizedStruct>(ch1.get(), bytes, sizeof(bytes));
    ASSERT_EQ(ZX_OK, iovec_message->status()) << iovec_message->error();

    auto result = reinterpret_cast<fidl_linearized::FullyLinearizedStruct*>(bytes);
    EXPECT_EQ(1, result->ptr->x);

    thread.join();
  });
}
