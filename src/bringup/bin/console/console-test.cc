// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <lib/fidl/cpp/wire/client.h>

#include <fbl/string_buffer.h>
#include <zxtest/zxtest.h>

namespace {

// Verify that calling Read() returns data from the RxSource
TEST(ConsoleTestCase, Read) {
  constexpr size_t kReadSize = 10;
  constexpr size_t kWriteCount = kReadSize - 1;
  constexpr uint8_t kWrittenByte = 3;

  sync_completion_t rx_source_done;
  Console::RxSource rx_source = [write_count = kWriteCount,
                                 &rx_source_done](uint8_t* byte) mutable {
    if (write_count == 0) {
      sync_completion_signal(&rx_source_done);
      return ZX_ERR_SHOULD_WAIT;
    }

    *byte = kWrittenByte;
    write_count--;
    return ZX_OK;
  };
  Console::TxSink tx_sink = [](const uint8_t* buffer, size_t length) { return ZX_OK; };

  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_OK(endpoints.status_value());
  auto& [client_end, server_end] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));
  Console console(loop.dispatcher(), std::move(event1), std::move(event2), std::move(rx_source),
                  std::move(tx_sink), {});
  ASSERT_OK(sync_completion_wait_deadline(&rx_source_done, ZX_TIME_INFINITE));
  fidl::BindServer(loop.dispatcher(), std::move(server_end),
                   static_cast<fidl::WireServer<fuchsia_hardware_pty::Device>*>(&console));
  fidl::WireClient client(std::move(client_end), loop.dispatcher());

  client->Read(kReadSize).ThenExactlyOnce(
      [kWriteCount,
       kWrittenByte](fidl::WireUnownedResult<fuchsia_hardware_pty::Device::Read>& result) {
        ASSERT_OK(result.status());
        fit::result response = result.value();
        ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
        ASSERT_EQ(response.value()->data.count(), kWriteCount);
        for (uint8_t byte : response.value()->data) {
          ASSERT_EQ(byte, kWrittenByte);
        }
      });
  ASSERT_OK(loop.RunUntilIdle());
}

// Verify that calling Write() writes data to the TxSink
TEST(ConsoleTestCase, Write) {
  uint8_t kExpectedBuffer[] = "Hello World";
  size_t kExpectedLength = sizeof(kExpectedBuffer) - 1;

  // Cause the RX thread to exit
  Console::RxSource rx_source = [](uint8_t* byte) { return ZX_ERR_NOT_SUPPORTED; };
  Console::TxSink tx_sink = [kExpectedLength, &kExpectedBuffer](const uint8_t* buffer,
                                                                size_t length) {
    EXPECT_EQ(length, kExpectedLength);
    EXPECT_BYTES_EQ(buffer, kExpectedBuffer, length);
    return ZX_OK;
  };

  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_OK(endpoints.status_value());
  auto& [client_end, server_end] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));
  Console console(loop.dispatcher(), std::move(event1), std::move(event2), std::move(rx_source),
                  std::move(tx_sink), {});
  fidl::BindServer(loop.dispatcher(), std::move(server_end),
                   static_cast<fidl::WireServer<fuchsia_hardware_pty::Device>*>(&console));
  fidl::WireClient client(std::move(client_end), loop.dispatcher());

  client->Write(fidl::VectorView<uint8_t>::FromExternal(kExpectedBuffer, kExpectedLength))
      .ThenExactlyOnce(
          [kExpectedLength](fidl::WireUnownedResult<fuchsia_hardware_pty::Device::Write>& result) {
            ASSERT_OK(result.status());
            fit::result response = result.value();
            ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
            ASSERT_EQ(response.value()->actual_count, kExpectedLength);
          });
  ASSERT_OK(loop.RunUntilIdle());
}

// Verify that calling Log() writes data to the TxSink
TEST(ConsoleTestCase, Log) {
  const char kExpectedBuffer[] = "[00004.321] 00001:00002> [tag] INFO: Hello World\n";
  size_t kExpectedLength = sizeof(kExpectedBuffer) - 1;
  fbl::StringBuffer<Console::kMaxWriteSize> actual;

  // Cause the RX thread to exit
  Console::RxSource rx_source = [](uint8_t* byte) { return ZX_ERR_NOT_SUPPORTED; };
  Console::TxSink tx_sink = [&actual](const uint8_t* buffer, size_t length) {
    actual.Append(reinterpret_cast<const char*>(buffer), length);
    return ZX_OK;
  };

  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));
  Console console(nullptr, std::move(event1), std::move(event2), std::move(rx_source),
                  std::move(tx_sink), {});

  fidl::StringView tag = "tag";
  fuchsia_logger::wire::LogMessage log{
      .pid = 1,
      .tid = 2,
      .time = 4321000000,
      .severity = static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kInfo),
      .tags = fidl::VectorView<fidl::StringView>::FromExternal(&tag, 1),
      .msg = {"Hello World"},
  };
  ASSERT_OK(console.Log(log));

  EXPECT_EQ(actual.size(), kExpectedLength);
  EXPECT_STREQ(actual.c_str(), kExpectedBuffer);
}

// Verify that calling Log() does not write data to the TxSink if the tag is denied
TEST(ConsoleTestCase, LogDenyTag) {
  fbl::StringBuffer<Console::kMaxWriteSize> actual;

  // Cause the RX thread to exit
  Console::RxSource rx_source = [](uint8_t* byte) { return ZX_ERR_NOT_SUPPORTED; };
  Console::TxSink tx_sink = [](const uint8_t* buffer, size_t length) {
    ADD_FAILURE("Unexpected write");
    return ZX_OK;
  };

  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));
  Console console(nullptr, std::move(event1), std::move(event2), std::move(rx_source),
                  std::move(tx_sink), {"deny-tag"});

  fidl::StringView tag = "deny-tag";
  fuchsia_logger::wire::LogMessage log{
      .pid = 1,
      .tid = 2,
      .time = 4321000000,
      .severity = static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kInfo),
      .tags = fidl::VectorView<fidl::StringView>::FromExternal(&tag, 1),
      .msg = {"Goodbye World"},
  };
  ASSERT_OK(console.Log(log));
}

}  // namespace
