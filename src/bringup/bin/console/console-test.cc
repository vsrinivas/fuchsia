// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <lib/sync/completion.h>
#include <lib/syslog/logger.h>

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

  fbl::RefPtr<Console> console;
  ASSERT_OK(Console::Create(std::move(rx_source), std::move(tx_sink), {}, &console));
  ASSERT_OK(sync_completion_wait_deadline(&rx_source_done, ZX_TIME_INFINITE));

  uint8_t data[kReadSize] = {};
  size_t actual;
  ASSERT_OK(console->Read(reinterpret_cast<void*>(data), kReadSize, &actual));
  ASSERT_EQ(actual, kWriteCount);
  for (size_t i = 0; i < actual; ++i) {
    ASSERT_EQ(data[i], kWrittenByte);
  }
}

// Verify that calling Write() writes data to the TxSink
TEST(ConsoleTestCase, Write) {
  uint8_t kExpectedBuffer[] = u8"Hello World";
  size_t kExpectedLength = sizeof(kExpectedBuffer) - 1;

  // Cause the RX thread to exit
  Console::RxSource rx_source = [](uint8_t* byte) { return ZX_ERR_NOT_SUPPORTED; };
  Console::TxSink tx_sink = [kExpectedLength, &kExpectedBuffer](const uint8_t* buffer,
                                                                size_t length) {
    EXPECT_EQ(length, kExpectedLength);
    EXPECT_BYTES_EQ(buffer, kExpectedBuffer, length);
    return ZX_OK;
  };

  fbl::RefPtr<Console> console;
  ASSERT_OK(Console::Create(std::move(rx_source), std::move(tx_sink), {}, &console));

  size_t actual;
  ASSERT_OK(
      console->Write(reinterpret_cast<const void*>(kExpectedBuffer), kExpectedLength, &actual));
  ASSERT_EQ(actual, kExpectedLength);
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

  fbl::RefPtr<Console> console;
  ASSERT_OK(Console::Create(std::move(rx_source), std::move(tx_sink), {}, &console));

  fidl::StringView tag = "tag";
  llcpp::fuchsia::logger::LogMessage log{
      .pid = 1,
      .tid = 2,
      .time = 4321000000,
      .severity = FX_LOG_INFO,
      .tags = {fidl::unowned_ptr(&tag), 1},
      .msg = {"Hello World"},
  };
  ASSERT_OK(console->Log(std::move(log)));

  EXPECT_EQ(actual.size(), kExpectedLength);
  EXPECT_STR_EQ(actual.c_str(), kExpectedBuffer);
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

  fbl::RefPtr<Console> console;
  ASSERT_OK(Console::Create(std::move(rx_source), std::move(tx_sink), {"deny-tag"}, &console));

  fidl::StringView tag = "deny-tag";
  llcpp::fuchsia::logger::LogMessage log{
      .pid = 1,
      .tid = 2,
      .time = 4321000000,
      .severity = FX_LOG_INFO,
      .tags = {fidl::unowned_ptr(&tag), 1},
      .msg = {"Goodbye World"},
  };
  ASSERT_OK(console->Log(std::move(log)));
}

}  // namespace
