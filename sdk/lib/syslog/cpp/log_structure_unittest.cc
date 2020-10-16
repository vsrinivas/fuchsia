// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/logging_backend_fuchsia_private.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace syslog {

TEST(StructuredLogging, Log) {
  FX_SLOG(WARNING)("test_log", {"foo"_k = "bar"});

  // TODO(fxbug.dev/57482): Figure out how to verify this appropriately.
}

TEST(StructuredLogging, BackendDirect) {
  syslog_backend::WriteLog(syslog::LOG_WARNING, "foo.cc", 42, "fake tag", "condition",
                           "Log message");
  syslog_backend::WriteLogValue(syslog::LOG_WARNING, "foo.cc", 42, "fake tag", "condition",
                                {"foo"_k = 42});
  // TODO(fxbug.dev/57482): Figure out how to verify this appropriately.
}

TEST(StructuredLogging, PaddedWritePadsWithZeroes) {
  uint64_t fives;
  memset(&fives, 5, sizeof(fives));
  uint64_t buffer[2];
  memset(buffer, 5, sizeof(buffer));
  // Writing "hi" results in 26984 which is
  // 'h' written to byte 0 | 'i' written to byte 1
  // Bytes get reversed due to endianness so they are flipped
  // when combined.
  WritePaddedInternal(buffer, "hi", 2);
  ASSERT_EQ(buffer[0], static_cast<uint64_t>(26984));
  ASSERT_EQ(buffer[1], fives);
  buffer[0] = fives;
  buffer[1] = fives;
  WritePaddedInternal(buffer, "", 0);
  ASSERT_EQ(buffer[0], fives);
  ASSERT_EQ(buffer[1], fives);
}

}  // namespace syslog
