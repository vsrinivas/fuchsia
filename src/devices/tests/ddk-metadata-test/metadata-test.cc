// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zxtest/zxtest.h>

#include "src/devices/tests/ddk-metadata-test/metadata-test-bind.h"

namespace {

zx_device_t* ddk_test_dev;

const char* TEST_STRING = "testing 1 2 3";

TEST(MetadataTest, AddMetadata) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  status = device_get_metadata(ddk_test_dev, 1, buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_ERR_NOT_FOUND, "device_get_metadata did not return ZX_ERR_NOT_FOUND");

  status = device_get_metadata_size(ddk_test_dev, 1, &actual);
  ASSERT_EQ(status, ZX_ERR_NOT_FOUND, "device_get_metadata_size should return ZX_ERR_NOT_FOUND");

  status = device_add_metadata(ddk_test_dev, 1, TEST_STRING, strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "device_add_metadata failed");

  status = device_get_metadata_size(ddk_test_dev, 1, &actual);
  ASSERT_EQ(strlen(TEST_STRING) + 1, actual, "Incorrect output length was returned.");
  status = device_get_metadata(ddk_test_dev, 1, buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK, "device_get_metadata failed");
  ASSERT_EQ(actual, strlen(TEST_STRING) + 1, "");
  ASSERT_EQ(strcmp(buffer, TEST_STRING), 0, "");
}

TEST(MetadataTest, AddMetadataLargeInput) {
  size_t large_len = 1024u * 16;
  auto large = std::make_unique<char[]>(large_len);
  zx_status_t status = device_add_metadata(ddk_test_dev, 1, large.get(), large_len);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "device_add_metadata shoud return ZX_ERR_INVALID_ARGS");
}

TEST(MetadataTest, PublishMetadata) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  // This should fail since the path does not match us or our potential children.
  status = device_publish_metadata(ddk_test_dev, "/dev/misc/null", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

  // We are allowed to add metadata to own path.
  status = device_publish_metadata(ddk_test_dev, "/dev/test/test", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");

  status = device_get_metadata(ddk_test_dev, 2, buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK, "device_get_metadata failed");
  ASSERT_EQ(actual, strlen(TEST_STRING) + 1, "");
  ASSERT_EQ(strcmp(buffer, TEST_STRING), 0, "");

  // We are allowed to add metadata to our potential children.
  status = device_publish_metadata(ddk_test_dev, "/dev/test/test/child", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");
}

TEST(MetadataTest, PublishMetadataLargeInput) {
  size_t large_len = 1024u * 16;
  auto large = std::make_unique<char[]>(large_len);
  zx_status_t status =
      device_publish_metadata(ddk_test_dev, "/dev/test/test/child", 2, large.get(), large_len);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "device_add_metadata shoud return ZX_ERR_INVALID_ARGS");
}

TEST(MetadataTest, GetMetadataWouldOverflow) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  status = device_publish_metadata(ddk_test_dev, "/dev/test/test", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");

  status = device_get_metadata(ddk_test_dev, 2, buffer, 1, &actual);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "device_get_metadata overflowed buffer");
}

// A special LogSink that just redirects all output to zxlogf
class LogSink : public zxtest::LogSink {
 public:
  void Write(const char* format, ...) override {
    std::array<char, 1024> line_buf;
    va_list args;
    va_start(args, format);
    vsnprintf(line_buf.data(), line_buf.size(), format, args);
    va_end(args);
    line_buf[line_buf.size() - 1] = 0;
    zxlogf(INFO, "%s", line_buf.data());
  }
  void Flush() override {}
};

zx_status_t metadata_test_bind(void* ctx, zx_device_t* parent) {
  zxlogf(ERROR, "HERE IN BIND");
  zxtest::Runner::GetInstance()->mutable_reporter()->set_log_sink(std::make_unique<LogSink>());
  ddk_test_dev = parent;
  if (RUN_ALL_TESTS(0, nullptr) != 0) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

static zx_driver_ops_t metadata_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = metadata_test_bind,
};

}  // namespace

ZIRCON_DRIVER(metadata_test, metadata_test_driver_ops, "zircon", "0.1");
