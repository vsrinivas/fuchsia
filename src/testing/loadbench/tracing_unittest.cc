// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tracing.h"

#include <lib/zircon-internal/ktrace.h>

#include <fstream>
#include <regex>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
class MockTracing : public Tracing {
 public:
  MOCK_METHOD(size_t, ReadAndReturnBytesRead,
              (zx_handle_t handle, void* data, uint32_t offset, size_t len, size_t* bytes_read),
              (override));
};

std::ofstream OpenFile(std::string tracing_filepath) {
  std::ofstream file;
  file.open(tracing_filepath);
  return file;
}

TEST(TracingTest, StartSetsRunningToTrue) {
  Tracing tracing;

  tracing.Start(0);
  ASSERT_TRUE(tracing.running());
}

TEST(TracingTest, StopSetsRunningToFalse) {
  Tracing tracing;

  tracing.Stop();
  ASSERT_FALSE(tracing.running());
}

TEST(TracingTest, DestructorStopsTracing) {
  Tracing tracing;

  tracing.Start(0);
  EXPECT_TRUE(tracing.running());

  tracing.~Tracing();

  ASSERT_FALSE(tracing.running());
}

TEST(TracingTest, BasicWriteSucceeds) {
  Tracing tracing;

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_TRUE(tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WritingToForbiddenFileFails) {
  Tracing tracing;

  std::ofstream human_readable_file = OpenFile("/forbidden/unittest.ktrace");

  ASSERT_FALSE(tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableStopsTraces) {
  Tracing tracing;

  tracing.Start(0);

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_TRUE(tracing.WriteHumanReadable(human_readable_file));
  ASSERT_FALSE(tracing.running());
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableRetriesRead) {
  MockTracing mock_tracing;

  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Return(sizeof(ktrace_header_t) - 5))
      .WillRepeatedly(Return(0));

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_TRUE(mock_tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableRetriesReadAndHandlesFailure) {
  MockTracing mock_tracing;

  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Return(sizeof(ktrace_header_t) - 5))
      // Read retry keeps track of total bytes read in current pass, so return 4 to keep total
      // bytes_read less than sizeof(ktrace_header_t).
      .WillOnce(Return(4))
      .WillRepeatedly(Return(0));

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_FALSE(mock_tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableFailsWithZeroTagLength) {
  MockTracing mock_tracing;

  using ::testing::Invoke;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG(0x25, KTRACE_GRP_ALL, 0)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }));

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_FALSE(mock_tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesPayloads) {
  MockTracing mock_tracing;
  Tracing tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x25, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }))
      .WillOnce(Invoke([](zx_handle_t, void* data_buf, uint32_t offset, size_t len,
                          size_t* bytes_read) -> size_t {
        ktrace_header_t record = {
            .tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x25, KTRACE_GRP_ALL)),
            .tid = 0,
            .ts = 0,
        };

        if (len < sizeof(ktrace_header_t))
          return 0;

        ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
        *record_ptr = record;
        record_ptr += offset;

        *bytes_read = sizeof(record);

        return sizeof(record);
      }))
      .WillRepeatedly(Return(0));

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_TRUE(mock_tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesLargeEvents) {
  MockTracing mock_tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke([](zx_handle_t, void* data_buf, uint32_t, size_t len,
                          size_t* bytes_read) -> size_t {
        ktrace_header_t record = {
            .tag = static_cast<uint32_t>(KTRACE_TAG_32B(static_cast<size_t>(-1), KTRACE_GRP_ALL)),
            .tid = 0,
            .ts = 0,
        };

        if (len < sizeof(ktrace_header_t))
          return 0;

        ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
        *record_ptr = record;

        *bytes_read = sizeof(record);

        return sizeof(record);
      }))
      .WillRepeatedly(Return(0));

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");

  ASSERT_TRUE(mock_tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableWritesCorrectFormat16B) {
  MockTracing mock_tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_16B(0x33, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }))
      .WillRepeatedly(Return(0));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex k16BFormat{"^[0-9]+: [a-zA-Z_]+\\(0x[0-9a-f]+\\), arg 0x[0-9a-f]+$"};
  std::smatch match;
  ASSERT_TRUE(std::regex_search(first_line, match, k16BFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableWritesCorrectFormat32B) {
  MockTracing mock_tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x1, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }))
      .WillRepeatedly(Return(0));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex k32BFormat{
      "^[0-9]+: [a-zA-Z_]+\\(0x[0-9a-e]+\\), tid 0x[0-9a-f]+, a 0x[0-9a-f]+, b 0x[0-9a-f]+, c "
      "0x[0-9a-f]+, d 0x[0-9a-f]+$"};
  std::smatch match;
  ASSERT_TRUE(std::regex_search(first_line, match, k32BFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableWritesCorrectFormatName) {
  MockTracing mock_tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_NAME(0x25, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }))
      .WillRepeatedly(Return(0));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kNameFormat{
      "^[a-zA-Z_]+\\(0x[0-9a-f]+\\), id 0x[0-9a-f]+, arg 0x[0-9a-f]+, .*$"};
  std::smatch match;
  ASSERT_TRUE(std::regex_search(first_line, match, kNameFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableWritesCorrectFormatUnexpectedEvent) {
  MockTracing mock_tracing;

  using ::testing::Invoke;
  using ::testing::Return;

  EXPECT_CALL(mock_tracing, ReadAndReturnBytesRead)
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) -> size_t {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_NAME(0xFFF, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < sizeof(ktrace_header_t))
              return 0;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;

            *bytes_read = sizeof(record);

            return sizeof(record);
          }))
      .WillRepeatedly(Return(0));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kUnexpectedFormat{"^Unexpected event: 0x[0-9a-f]+$"};
  std::smatch match;
  ASSERT_TRUE(std::regex_search(first_line, match, kUnexpectedFormat));

  in_file.close();
}
}  // anonymous namespace
