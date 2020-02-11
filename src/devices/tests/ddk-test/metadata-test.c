// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/device.h>
#include <unittest/unittest.h>

extern zx_device_t* ddk_test_dev;

static const char* TEST_STRING = "testing 1 2 3";

static bool test_add_metadata(void) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  BEGIN_TEST;

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

  END_TEST;
}

static bool test_add_metadata_large_input(void) {
  BEGIN_TEST;

  size_t large_len = 1024u * 16;
  char* large = malloc(large_len);
  ASSERT_NE(large, NULL, "allocation failure");
  zx_status_t status = device_add_metadata(ddk_test_dev, 1, large, large_len);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "device_add_metadata shoud return ZX_ERR_INVALID_ARGS");
  free(large);

  END_TEST;
}

static bool test_publish_metadata(void) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  BEGIN_TEST;
  // This should fail since the path does not match us or our potential children.
  status = device_publish_metadata(ddk_test_dev, "/dev/misc/null", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

  // We are allowed to add metadata to own path.
  status = device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");

  status = device_get_metadata(ddk_test_dev, 2, buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK, "device_get_metadata failed");
  ASSERT_EQ(actual, strlen(TEST_STRING) + 1, "");
  ASSERT_EQ(strcmp(buffer, TEST_STRING), 0, "");

  // We are allowed to add metadata to our potential children.
  status = device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test/child", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");

  END_TEST;
}

static bool test_publish_metadata_large_input(void) {
  BEGIN_TEST;

  size_t large_len = 1024u * 16;
  char* large = malloc(large_len);
  ASSERT_NE(large, NULL, "allocation failure");
  zx_status_t status =
      device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test/child", 2, large, large_len);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "device_add_metadata shoud return ZX_ERR_INVALID_ARGS");
  free(large);

  END_TEST;
}

static bool test_get_metadata_would_overflow(void) {
  char buffer[32] = {};
  zx_status_t status;
  size_t actual;

  BEGIN_TEST;
  status = device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test", 2, TEST_STRING,
                                   strlen(TEST_STRING) + 1);
  ASSERT_EQ(status, ZX_OK, "");

  status = device_get_metadata(ddk_test_dev, 2, buffer, 1, &actual);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "device_get_metadata overflowed buffer");

  END_TEST;
}

BEGIN_TEST_CASE(metadata_tests)
RUN_TEST(test_add_metadata)
RUN_TEST(test_add_metadata_large_input)
RUN_TEST(test_publish_metadata)
RUN_TEST(test_publish_metadata_large_input)
RUN_TEST(test_get_metadata_would_overflow)
END_TEST_CASE(metadata_tests)

struct test_case_element* test_case_ddk_metadata = TEST_CASE_ELEMENT(metadata_tests);
