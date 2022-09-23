// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/internal/hexdump.h>
#include <wlan/drivers/log_instance.h>

#include "log_test.h"

namespace wlan::drivers {
class HexDumpTest : public LogTest {
 public:
  void SetUp() override {
    LogTest::SetUp();
    for (uint8_t i = 0; i < kDataSize; i++) {
      data_[i] = static_cast<char>(i);
    }
  }

 protected:
  static constexpr uint8_t kDataSize = 100;
  char data_[kDataSize];
  char outbuf_[log::kHexDumpMinBufSize];
  uint8_t data16B_[log::kHexDumpMaxBytesPerLine] = {
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0xde, 0xad, 0xbe, 0xef, 0x55, 0x66, 0x01, 0x83,
  };
  static constexpr size_t kStrStartOffset =
      (log::kHexDumpMaxBytesPerLine * log::kCharPerByte) + log::kSpaceBetHexAndStr;
};

TEST_F(HexDumpTest, HexSanity) {
  lhexdump_error(data_, sizeof(data_));
  lhexdump_warn(data_, sizeof(data_));
  lhexdump_info(data_, sizeof(data_));
  log::Instance::Init(0x3);
  lhexdump_debug(0x1, kDebugTag, data_, sizeof(data_));
  lhexdump_trace(0x2, kTraceTag, data_, sizeof(data_));
}

TEST_F(HexDumpTest, HexDumpError) {
  lhexdump_error(data_, sizeof(data_));
  Validate(DDK_LOG_ERROR);
}

TEST_F(HexDumpTest, HexDumpWarn) {
  lhexdump_warn(data_, sizeof(data_));
  Validate(DDK_LOG_WARNING);
}

TEST_F(HexDumpTest, HexDumpInfo) {
  lhexdump_info(data_, sizeof(data_));
  Validate(DDK_LOG_INFO);
}

TEST_F(HexDumpTest, HexDumpDebugFiltered) {
  log::Instance::Init(0);
  lhexdump_debug(0x1, kDebugTag, data_, sizeof(data_));
  ASSERT_FALSE(LogInvoked());
}

TEST_F(HexDumpTest, HexDumpDebugNotFiltered) {
  log::Instance::Init(0x1);
  lhexdump_debug(0x1, kDebugTag, data_, sizeof(data_));
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

TEST_F(HexDumpTest, HexDumpTraceFiltered) {
  log::Instance::Init(0);
  lhexdump_trace(0x2, kTraceTag, data_, sizeof(data_));
  ASSERT_FALSE(LogInvoked());
}

TEST_F(HexDumpTest, HexDumpTraceNotFiltered) {
  log::Instance::Init(0x2);
  lhexdump_trace(0x2, kTraceTag, data_, sizeof(data_));
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_TRACE, kTraceTag);
}

TEST_F(HexDumpTest, HexDumpErrorHandling) {
  // Insufficient output buffer size.
  outbuf_[0] = 'a';
  log::HexDump(data16B_, sizeof(data16B_), outbuf_, sizeof(outbuf_) - 1);
  ASSERT_EQ('\0', outbuf_[0]);

  // Data too large.
  outbuf_[0] = 'a';
  log::HexDump(data16B_, sizeof(data16B_) + 1, outbuf_, sizeof(outbuf_));
  ASSERT_EQ('\0', outbuf_[0]);
}

TEST_F(HexDumpTest, HexDumpExactly16Bytes) {
  log::HexDump(data16B_, sizeof(data16B_), outbuf_, sizeof(outbuf_));

  // Hex value part
  EXPECT_EQ('0', outbuf_[0]);  // the first byte: 0x01
  EXPECT_EQ('1', outbuf_[1]);
  EXPECT_EQ(' ', outbuf_[2]);
  EXPECT_EQ('8', outbuf_[15 * 3 + 0]);  // the last byte: 0x83
  EXPECT_EQ('3', outbuf_[15 * 3 + 1]);
  EXPECT_EQ(' ', outbuf_[15 * 3 + 2]);

  // ASCII part
  EXPECT_EQ(log::kNP, outbuf_[kStrStartOffset]);          // non-printable
  EXPECT_EQ('E', outbuf_[kStrStartOffset + 2]);           // printable
  EXPECT_EQ(log::kNP, outbuf_[kStrStartOffset + 4]);      // non-printable
  EXPECT_EQ(log::kNP, outbuf_[kStrStartOffset + 5]);      // the last byte: non-printable
  EXPECT_EQ('\0', outbuf_[log::kHexDumpMinBufSize - 1]);  // null-terminator
}

TEST_F(HexDumpTest, HexDumpLessThan16Bytes) {
  uint8_t data[] = {
      0x61,
  };

  log::HexDump(data, sizeof(data), outbuf_, sizeof(outbuf_));

  // Hex value part
  EXPECT_EQ('6', outbuf_[0]);  // the first byte: 0x61
  EXPECT_EQ('1', outbuf_[1]);
  EXPECT_EQ(' ', outbuf_[2]);
  EXPECT_EQ(' ', outbuf_[3]);  // the second byte: not dumped.
  EXPECT_EQ(' ', outbuf_[4]);
  EXPECT_EQ(' ', outbuf_[5]);

  // ASCII part
  EXPECT_EQ('a', outbuf_[kStrStartOffset]);               // printable
  EXPECT_EQ(' ', outbuf_[kStrStartOffset + 1]);           // the second byte: not dumped.
  EXPECT_EQ('\0', outbuf_[log::kHexDumpMinBufSize - 1]);  // null-terminator
}

TEST_F(HexDumpTest, HexDumpZeroByte) {
  uint8_t data[] = {};

  log::HexDump(data, sizeof(data), outbuf_, sizeof(outbuf_));

  // Hex value part
  EXPECT_EQ(' ', outbuf_[0]);  // nothing dumped
  EXPECT_EQ(' ', outbuf_[1]);
  EXPECT_EQ(' ', outbuf_[2]);

  // ASCII part
  EXPECT_EQ(' ', outbuf_[kStrStartOffset]);               // nothing dumped
  EXPECT_EQ('\0', outbuf_[log::kHexDumpMinBufSize - 1]);  // null-terminator
}

}  // namespace wlan::drivers
