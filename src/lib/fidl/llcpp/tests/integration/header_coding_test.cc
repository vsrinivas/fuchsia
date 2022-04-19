// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains tests that break down the process of sending and
// receiving messages through the bindings. The intent is to make it
// easier to debug encoding and decoding issues that result in a header
// not being included or properly handled in the message.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/cpp/completion.h>

#include <zxtest/zxtest.h>

namespace {

using ::fidl_test_coding_fuchsia::Example;

constexpr const char kMessageString[] = "abcd";

template <size_t N>
constexpr size_t stringLen(const char (&s)[N]) {
  return N - 1;
}

TEST(HeaderCodingTest, OneWay) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Example> client(std::move(client_end), loop.dispatcher());

  auto result = client->OneWay(kMessageString);
  ASSERT_OK(result.status());

  constexpr size_t kStringOutOfLineSize = FIDL_ALIGN(stringLen(kMessageString));
  uint8_t buffer[sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::OneWay>) +
                 kStringOutOfLineSize];

  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = server_end.TakeHandle().read(0, buffer, nullptr, std::size(buffer), 0,
                                                    &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_EQ(std::size(buffer), actual_bytes);
  ASSERT_EQ(0, actual_handles);

  const auto* header = reinterpret_cast<fidl_message_header_t*>(buffer);
  ASSERT_EQ(kFidlWireFormatMagicNumberInitial, header->magic_number);
  ASSERT_EQ(FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, header->at_rest_flags[0]);
  ASSERT_EQ(FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD, header->dynamic_flags);
  ASSERT_EQ(fidl::internal::WireOrdinal<Example::OneWay>::value, header->ordinal);

  ASSERT_BYTES_EQ(
      buffer + sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::OneWay>),
      kMessageString, stringLen(kMessageString));
}

TEST(HeaderCodingTest, TwoWayAsync) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Example> client(std::move(client_end), loop.dispatcher());

  libsync::Completion completion;
  client->TwoWay(kMessageString)
      .ThenExactlyOnce([&completion](fidl::WireUnownedResult<Example::TwoWay>& result) {
        ASSERT_TRUE(result.ok());
        ASSERT_BYTES_EQ(result.value().out.data(), kMessageString, stringLen(kMessageString));
        completion.Signal();
      });

  constexpr size_t kStringOutOfLineSize = FIDL_ALIGN(4);
  uint8_t buffer[sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::TwoWay>) +
                 kStringOutOfLineSize];

  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx::channel server_ch = server_end.TakeHandle();
  zx_status_t status =
      server_ch.read(0, buffer, nullptr, std::size(buffer), 0, &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_EQ(std::size(buffer), actual_bytes);
  ASSERT_EQ(0, actual_handles);

  const auto* header = reinterpret_cast<fidl_message_header_t*>(buffer);
  ASSERT_EQ(kFidlWireFormatMagicNumberInitial, header->magic_number);
  ASSERT_EQ(FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, header->at_rest_flags[0]);
  ASSERT_EQ(FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD, header->dynamic_flags);
  ASSERT_EQ(fidl::internal::WireOrdinal<Example::TwoWay>::value, header->ordinal);

  ASSERT_BYTES_EQ(
      buffer + sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::OneWay>),
      kMessageString, stringLen(kMessageString));

  status = server_ch.write(0, buffer, actual_bytes, nullptr, actual_handles);
  ASSERT_OK(status);

  completion.Wait();
}

TEST(HeaderCodingTest, TwoWaySync) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);

  std::thread th([server_ch = server_end.TakeHandle()]() {
    server_ch.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);

    constexpr size_t kStringOutOfLineSize = FIDL_ALIGN(stringLen(kMessageString));
    uint8_t buffer[sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::TwoWay>) +
                   kStringOutOfLineSize];

    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status =
        server_ch.read(0, buffer, nullptr, std::size(buffer), 0, &actual_bytes, &actual_handles);
    ASSERT_OK(status);
    ASSERT_EQ(std::size(buffer), actual_bytes);
    ASSERT_EQ(0, actual_handles);

    const auto* header = reinterpret_cast<fidl_message_header_t*>(buffer);
    ASSERT_EQ(kFidlWireFormatMagicNumberInitial, header->magic_number);
    ASSERT_EQ(FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, header->at_rest_flags[0]);
    ASSERT_EQ(FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD, header->dynamic_flags);
    ASSERT_EQ(fidl::internal::WireOrdinal<Example::TwoWay>::value, header->ordinal);

    ASSERT_BYTES_EQ(
        buffer + sizeof(fidl_message_header_t) + sizeof(fidl::WireRequest<Example::OneWay>),
        kMessageString, stringLen(kMessageString));

    status = server_ch.write(0, buffer, actual_bytes, nullptr, actual_handles);
    ASSERT_OK(status);
  });

  fidl::WireSyncClient<Example> client(std::move(client_end));
  fidl::WireResult<Example::TwoWay> result = client->TwoWay(kMessageString);
  ASSERT_TRUE(result.ok());
  ASSERT_BYTES_EQ(result.value().out.data(), kMessageString, stringLen(kMessageString));

  th.join();
}

}  // namespace
