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

using ::testing::Invoke;
using ::testing::Return;

namespace {
class MockTracing : public Tracing {
 public:
  MOCK_METHOD(void, ReadKernelBuffer,
              (zx_handle_t handle, void* data_buf, uint32_t offset, size_t len, size_t* bytes_read),
              (override));

  MOCK_METHOD((std::tuple<bool, bool>), FetchRecord,
              (zx_handle_t handle, uint8_t* data_buf, uint32_t* offset, size_t* bytes_read,
               size_t buf_len),
              (override));

  std::tuple<bool, bool> RealFetchRecord(zx_handle_t handle, uint8_t* data_buf, uint32_t* offset,
                                         size_t* bytes_read, size_t buf_len) {
    return Tracing::FetchRecord(handle, data_buf, offset, bytes_read, buf_len);
  }
};

class CreateMockKernelRead {
 public:
  CreateMockKernelRead(uint32_t tag, uint32_t tid, uint64_t ts) {
    tag_ = tag;
    tid_ = tid;
    ts_ = ts;
  }

  void operator()(zx_handle_t, void* data_buf, uint32_t, size_t len, size_t* bytes_read) {
    ktrace_header_t record = {
        .tag = tag_,
        .tid = tid_,
        .ts = ts_,
    };

    if (len < KTRACE_HDRSIZE)
      return;

    ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
    *record_ptr = record;

    *bytes_read = sizeof(record);
  }

 private:
  uint32_t tag_;
  uint32_t tid_;
  uint64_t ts_;
};

class CreateMockTrace {
 public:
  CreateMockTrace(uint32_t tag, uint32_t tid, uint64_t ts) {
    tag_ = tag;
    tid_ = tid;
    ts_ = ts;
  }

  std::tuple<bool, bool> operator()(zx_handle_t, uint8_t* data_buf, uint32_t*, size_t*, size_t) {
    ktrace_header_t* record = reinterpret_cast<ktrace_header_t*>(data_buf);

    record->tag = tag_;
    record->tid = tid_;
    record->ts = ts_;

    return {true, false};
  }

 private:
  uint32_t tag_;
  uint32_t tid_;
  uint64_t ts_;
};

class ReaderHelper {
 public:
  void ReadZeroBytes(zx_handle_t, void*, uint32_t, size_t, size_t* bytes_read) { *bytes_read = 0; }
  std::tuple<bool, bool> ReturnEndOfKernelBuffer(zx_handle_t, uint8_t*, uint32_t*, size_t*,
                                                 size_t) {
    return {true, true};
  }
};

std::ofstream OpenFile(std::string tracing_filepath) {
  std::ofstream file;
  file.open(tracing_filepath);
  return file;
}

TEST(TracingTest, StartSetsRunningToTrue) {
  Tracing tracing;

  tracing.Start(KTRACE_GRP_ALL);
  ASSERT_TRUE(tracing.running());
}

TEST(TracingTest, StopSetsRunningToFalse) {
  Tracing tracing;

  tracing.Stop();
  ASSERT_FALSE(tracing.running());
}

TEST(TracingTest, DestructorStopsTracing) {
  Tracing tracing;

  tracing.Start(KTRACE_GRP_ALL);
  EXPECT_TRUE(tracing.running());

  tracing.~Tracing();

  ASSERT_FALSE(tracing.running());
}

TEST(TracingTest, BasicWriteSucceeds) {
  Tracing tracing;

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");
  ASSERT_TRUE(human_readable_file);

  ASSERT_TRUE(tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, WritingToForbiddenFileFails) {
  Tracing tracing;

  std::ofstream human_readable_file = OpenFile("/forbidden/unittest.ktrace");
  ASSERT_FALSE(human_readable_file);

  ASSERT_FALSE(tracing.WriteHumanReadable(human_readable_file));
  human_readable_file.close();
}

TEST(TracingTest, FetchRecordRetriesReadAndHandlesFailure) {
  MockTracing mock_tracing;

  const size_t buf_len = 256;
  uint8_t data_buf[buf_len];
  size_t bytes_read = 0;
  uint32_t offset = 0;

  EXPECT_CALL(mock_tracing, ReadKernelBuffer)
      .WillOnce(Invoke([](zx_handle_t, void*, uint32_t, size_t, size_t* bytes_read) {
        *bytes_read = KTRACE_HDRSIZE - 5;
      }))
      .WillOnce(
          Invoke([](zx_handle_t, void*, uint32_t, size_t, size_t* bytes_read) { *bytes_read = 0; }))
      // Failure case.
      .WillOnce(Invoke([](zx_handle_t, void*, uint32_t, size_t, size_t* bytes_read) {
        *bytes_read = KTRACE_HDRSIZE - 5;
      }))
      // Read retry keeps track of total bytes read in current pass, so return 4 to keep total
      // bytes_read less than KTRACE_HDRSIZE.
      .WillOnce(Invoke(
          [](zx_handle_t, void*, uint32_t, size_t, size_t* bytes_read) { *bytes_read = 4; }));

  auto [read_success, buf_end] =
      mock_tracing.RealFetchRecord(0, data_buf, &offset, &bytes_read, buf_len);
  ASSERT_TRUE(read_success);

  auto [read_fail, buf_end_fail] =
      mock_tracing.RealFetchRecord(0, data_buf, &offset, &bytes_read, buf_len);
  ASSERT_FALSE(read_fail);
}

TEST(TracingTest, FetchRecordFailsWithZeroTagLength) {
  MockTracing mock_tracing;
  Tracing tracing;

  const size_t buf_len = 256;
  uint8_t data_buf[buf_len];
  size_t bytes_read = 0;
  uint32_t offset = 0;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG(0x25, KTRACE_GRP_ALL, 0));
  CreateMockKernelRead zero_tag_length(mock_tag, 0, 0);

  EXPECT_CALL(mock_tracing, ReadKernelBuffer).WillOnce(Invoke(zero_tag_length));

  auto [read_success, buffer_end] =
      mock_tracing.RealFetchRecord(0, data_buf, &offset, &bytes_read, buf_len);
  ASSERT_FALSE(read_success);
}

TEST(TracingTest, FetchRecordHandlesPayloads) {
  MockTracing mock_tracing;

  const size_t buf_len = 256;
  uint8_t data_buf[buf_len];
  size_t bytes_read = 0;
  uint32_t offset = 0;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x25, KTRACE_GRP_ALL));
  CreateMockKernelRead payload_present(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, ReadKernelBuffer)
      .WillOnce(Invoke(payload_present))
      .WillOnce(Invoke(
          [](zx_handle_t, void* data_buf, uint32_t offset, size_t len, size_t* bytes_read) -> void {
            ktrace_header_t record = {
                .tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x25, KTRACE_GRP_ALL)),
                .tid = 0,
                .ts = 0,
            };

            if (len < KTRACE_HDRSIZE)
              return;

            ktrace_header_t* record_ptr = reinterpret_cast<ktrace_header_t*>(data_buf);
            *record_ptr = record;
            record_ptr += offset;

            *bytes_read = sizeof(record);
          }))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReadZeroBytes));

  auto [read_success, buffer_end] =
      mock_tracing.RealFetchRecord(0, data_buf, &offset, &bytes_read, buf_len);
  ASSERT_TRUE(read_success);
}

TEST(TracingTest, FetchRecordHandlesSmallDataBuffers) {
  Tracing tracing;

  auto [read_success, buffer_end] = tracing.FetchRecord(0, nullptr, 0, nullptr, KTRACE_HDRSIZE - 1);
  ASSERT_FALSE(read_success);
}

TEST(TracingTest, ParseRecordHandlesLargeEvents) {
  const size_t buf_len = 256;
  uint8_t* data_buf;

  ktrace_header_t record = {
      .tag = static_cast<uint32_t>(KTRACE_TAG_32B(static_cast<size_t>(-1), KTRACE_GRP_ALL)),
      .tid = 0,
      .ts = 0,
  };

  data_buf = reinterpret_cast<uint8_t*>(&record);

  auto ktrace_record_opt = KTraceRecord::ParseRecord(data_buf, buf_len);
  ASSERT_TRUE(ktrace_record_opt);
  ASSERT_TRUE(ktrace_record_opt.value().HasUnexpectedEvent());
}

TEST(TracingTest, ParseRecordHandlesSmallBuffers) {
  KTraceRecord ktrace_record;

  ASSERT_FALSE(ktrace_record.ParseRecord(nullptr, KTRACE_HDRSIZE - 1));
}

TEST(TracingTest, RecordGettersDoNotReturnNullPointers) {
  KTraceRecord ktrace_record;

  ktrace_header_t* rec_header;
  ktrace_rec_32b_t* rec_32b;
  ktrace_rec_name_t* rec_name;

  ASSERT_FALSE(ktrace_record.Get16BRecord(&rec_header));
  ASSERT_FALSE(ktrace_record.Get32BRecord(&rec_32b));
  ASSERT_FALSE(ktrace_record.GetNameRecord(&rec_name));
  ASSERT_FALSE(ktrace_record.Get64BitPayload());
  ASSERT_FALSE(ktrace_record.Get128BitPayload());
  ASSERT_FALSE(ktrace_record.GetFlowID());
}

TEST(TracingTest, WriteHumanReadableStopsTraces) {
  Tracing tracing;

  tracing.Start(KTRACE_GRP_ALL);

  std::ofstream human_readable_file = OpenFile("/tmp/unittest.ktrace");
  ASSERT_TRUE(human_readable_file);

  ASSERT_TRUE(tracing.WriteHumanReadable(human_readable_file));
  ASSERT_FALSE(tracing.running());
  human_readable_file.close();
}

TEST(TracingTest, WriteHumanReadableWritesCorrectFormat16B) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_16B(0x33, KTRACE_GRP_ALL));
  CreateMockTrace correct_format_16_B(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_16_B))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

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

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_32B(0x1, KTRACE_GRP_ALL));
  CreateMockTrace correct_format_32_B(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_32_B))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

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

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_NAME(0x25, KTRACE_GRP_ALL));
  CreateMockTrace correct_format_name(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_name))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

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

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_NAME(0xFFF, KTRACE_GRP_ALL));
  CreateMockTrace correct_format_unexpected_event(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_unexpected_event))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

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

TEST(TracingTest, WriteHumanReadableHandlesProbeRecord16) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_PROBE, 16, 1));
  CreateMockTrace correct_format_probe_record_16(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_probe_record_16))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kProbeRecord16Format{
      "^PROBE: tag 0x[0-9a-f]+, event_name_id 0x[0-9a-f]+, tid 0x[0-9a-f]+, ts [0-9]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kProbeRecord16Format));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesProbeRecord24) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_PROBE, 24, 1));
  CreateMockTrace correct_format_probe_record_24(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_probe_record_24))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kProbeRecord24Format{
      "^PROBE: tag 0x[0-9a-f]+, event_name_id 0x[0-9a-f]+, tid 0x[0-9a-f]+, ts [0-9]+, a "
      "0x[0-9a-f]+, b 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kProbeRecord24Format));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesProbeRecord32) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_PROBE, 32, 1));
  CreateMockTrace correct_format_probe_record_32(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_probe_record_32))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kProbeRecord32Format{
      "^PROBE: tag 0x[0-9a-f]+, event_name_id 0x[0-9a-f]+, tid 0x[0-9a-f]+, ts [0-9]+, a "
      "0x[0-9a-f]+, b 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kProbeRecord32Format));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesProbeRecordUnexpectedSize) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_PROBE, 0xFFF, 1));
  CreateMockTrace correct_format_probe_record_unexpected_size(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_probe_record_unexpected_size))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kProbeRecordUnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kProbeRecordUnexpectedFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord16Begin) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 16, KTRACE_FLAGS_BEGIN));
  CreateMockTrace correct_format_duration_record_16_begin(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_16_begin))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord16BeginFormat{
      "^[0-9]+: DURATION BEGIN: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord16BeginFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord16End) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 16, KTRACE_FLAGS_END));
  CreateMockTrace correct_format_duration_record_16_end(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_16_end))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord16EndFormat{
      "^[0-9]+: DURATION END: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord16EndFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord16Unexpected) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 16, 1));
  CreateMockTrace correct_format_duration_record_16_unexpected(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_16_unexpected))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord16UnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord16UnexpectedFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord32Begin) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, KTRACE_FLAGS_BEGIN));
  CreateMockTrace correct_format_duration_record_32_begin(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_32_begin))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord32BeginFormat{
      "^[0-9]+: DURATION BEGIN: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+, a 0x[0-9a-f]+, b "
      "0x"
      "[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord32BeginFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord32End) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, KTRACE_FLAGS_END));
  CreateMockTrace correct_format_duration_record_32_end(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_32_end))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord32EndFormat{
      "^[0-9]+: DURATION END: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+, a 0x[0-9a-f]+, b 0x"
      "[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord32EndFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord32Unexpected) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, 1));
  CreateMockTrace correct_format_duration_record_32_unexpected(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_32_unexpected))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecord32UnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecord32UnexpectedFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesDurationRecord32UnexpectedSize) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 0xFFF, KTRACE_FLAGS_BEGIN));
  CreateMockTrace correct_format_duration_record_32_unexpected_size(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_duration_record_32_unexpected_size))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kDurationRecordUnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kDurationRecordUnexpectedFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesFlowRecord32Begin) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(
      KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, KTRACE_FLAGS_FLOW | KTRACE_FLAGS_BEGIN));
  CreateMockTrace correct_format_flow_record_32_begin(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_flow_record_32_begin))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kFlowRecord32BeginFormat{
      "^[0-9]+: FLOW BEGIN: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+, flow id "
      "0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kFlowRecord32BeginFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesFlowRecord32End) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(
      KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, KTRACE_FLAGS_FLOW | KTRACE_FLAGS_END));
  CreateMockTrace correct_format_flow_record_32_end(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_flow_record_32_end))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kFlowRecord32EndFormat{
      "^[0-9]+: FLOW END: tag 0x[0-9a-f]+, id 0x[0-9a-f]+, tid 0x[0-9a-f]+, flow id "
      "0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kFlowRecord32EndFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesFlowRecord32Unexpected) {
  MockTracing mock_tracing;

  uint32_t mock_tag =
      static_cast<uint32_t>(KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 32, KTRACE_FLAGS_FLOW));
  CreateMockTrace correct_format_flow_record_32_unexpected(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_flow_record_32_unexpected))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kFlowRecord32UnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kFlowRecord32UnexpectedFormat));

  in_file.close();
}

TEST(TracingTest, WriteHumanReadableHandlesFlowRecordUnexpectedSize) {
  MockTracing mock_tracing;

  uint32_t mock_tag = static_cast<uint32_t>(
      KTRACE_TAG_EX(0x25, KTRACE_GRP_SCHEDULER, 0xFFF, KTRACE_FLAGS_FLOW | KTRACE_FLAGS_BEGIN));
  CreateMockTrace correct_format_flow_record_unexpected_size(mock_tag, 0, 0);
  ReaderHelper reader_helper;

  EXPECT_CALL(mock_tracing, FetchRecord)
      .WillOnce(Invoke(correct_format_flow_record_unexpected_size))
      .WillRepeatedly(Invoke(&reader_helper, &ReaderHelper::ReturnEndOfKernelBuffer));

  std::string filepath = "/tmp/unittest.ktrace";
  std::ofstream human_readable_file = OpenFile(filepath);
  ASSERT_TRUE(human_readable_file);

  mock_tracing.WriteHumanReadable(human_readable_file);
  human_readable_file.close();

  std::ifstream in_file;
  in_file.open(filepath);
  ASSERT_TRUE(in_file);

  std::string first_line;
  getline(in_file, first_line);

  const std::regex kFlowRecordUnexpectedFormat{"^Unexpected tag: 0x[0-9a-f]+$"};
  std::smatch match;

  ASSERT_TRUE(std::regex_search(first_line, match, kFlowRecordUnexpectedFormat));

  in_file.close();
}
}  // anonymous namespace
