// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the debuglog.

#include <zxtest/zxtest.h>
#include <zircon/syscalls/log.h>

// This is provided by utest/core/main.c
extern "C" __WEAK zx_handle_t get_root_resource();

namespace {

TEST(DebugLogTest, WriteRead) {
  zx_handle_t log_handle = 0;
  ASSERT_OK(zx_debuglog_create(get_root_resource(), ZX_LOG_FLAG_READABLE, &log_handle));

  // Ensure something is written.
  static const char kTestMsg[] = "Debuglog test message.\n";
  ASSERT_OK(zx_debuglog_write(log_handle, 0, kTestMsg, sizeof(kTestMsg)));

  // In-case the read bound isn't respected create a buffer large enough to hopefully
  // prevent corruption.
  char buf[10240]{0};

  // But only report a smaller size for the buffer.
  const size_t read_len = 3;
  const auto status_or_size = zx_debuglog_read(log_handle, 0, buf, read_len);
  ASSERT_EQ(read_len, status_or_size);

  // Ensure that only read_len bytes were written to our buffer.
  const char empty[10]{0};
  ASSERT_EQ(0, memcmp(buf + read_len, empty, sizeof(empty)));

  ASSERT_OK(zx_handle_close(log_handle));
}

}  // namespace
