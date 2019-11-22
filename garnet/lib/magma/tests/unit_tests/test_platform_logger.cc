// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <platform_logger.h>
#include <string.h>

#include <gtest/gtest.h>

TEST(PlatformLogger, LogMacro) {
  // Assumes the logger has already been initialized.
  ASSERT_TRUE(magma::PlatformLogger::IsInitialized());
  MAGMA_LOG(INFO, "%s %s", "Hello", "world!");
}

TEST(PlatformLogger, LogFrom) {
  // Assumes the logger has already been initialized.
  ASSERT_TRUE(magma::PlatformLogger::IsInitialized());
  magma::PlatformLogger::LogFrom(magma::PlatformLogger::LOG_INFO, __FILE__, __LINE__, "%s %s",
                                 "Hello", "world!");
}

static void format_buffer(char out[], const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  magma::PlatformLogger::FormatBuffer(out, nullptr, 0, msg, args);
  va_end(args);
}

constexpr char kPrefix[] = "Buffer's too big: ";

TEST(PlatformLogger, FormatBuffer) {
  char in[magma::PlatformLogger::kBufferSize];
  memset(in, '@', sizeof(in));
  in[sizeof(in) - 1] = 0;
  char out[magma::PlatformLogger::kBufferSize];
  format_buffer(out, nullptr, 0, "%s%s", kPrefix, in);
  for (size_t i = 0;
       i < magma::PlatformLogger::kBufferSize - magma::PlatformLogger::kSentinelSize - 1; i++) {
    if (i < sizeof(kPrefix) - 1) {
      EXPECT_EQ(out[i], kPrefix[i]) << "Index: " << i;
    } else {
      EXPECT_EQ(out[i], '@') << "Index: " << i;
    }
  }
  EXPECT_EQ(out[magma::PlatformLogger::kBufferSize - magma::PlatformLogger::kSentinelSize - 1],
            '\0')
      << "Null terminator";
}

TEST(PlatformLogger, FormatBufferWithFileAndLine) {
  char in[magma::PlatformLogger::kBufferSize];
  memset(in, '@', sizeof(in));
  in[sizeof(in) - 1] = 0;
  char out[magma::PlatformLogger::kBufferSize];
  format_buffer(out, nullptr, 0, "%s%s", kPrefix, in);
  for (size_t i = 0;
       i < magma::PlatformLogger::kBufferSize - magma::PlatformLogger::kSentinelSize - 1; i++) {
    if (i < sizeof(kPrefix) - 1) {
      EXPECT_EQ(out[i], kPrefix[i]) << "Index: " << i;
    } else {
      EXPECT_EQ(out[i], '@') << "Index: " << i;
    }
  }
  EXPECT_EQ(out[magma::PlatformLogger::kBufferSize - magma::PlatformLogger::kSentinelSize - 1],
            '\0')
      << "Null terminator";
}
