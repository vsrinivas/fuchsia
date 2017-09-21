// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/common/bt_snoop_logger.h"

#include <cstdio>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace common {
namespace {

constexpr char kTestLogFilePath[] = "/tmp/bt_snoop_logger_test.btsnoop";

class BTSnoopLoggerTest : public ::testing::Test {
 public:
  BTSnoopLoggerTest() = default;
  ~BTSnoopLoggerTest() override = default;

 protected:
  // ::testing::Test overrides:
  void TearDown() override {
    if (std::remove(kTestLogFilePath) != 0)
      FXL_LOG(WARNING) << "Failed to remove temporary test file";
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BTSnoopLoggerTest);
};

TEST_F(BTSnoopLoggerTest, SimpleInitialize) {
  auto logger = std::make_unique<BTSnoopLogger>();
  EXPECT_TRUE(logger->Initialize(kTestLogFilePath));

  // Already initialized
  EXPECT_FALSE(logger->Initialize(kTestLogFilePath));
  EXPECT_FALSE(logger->Initialize("foo"));

  // The file should contain just the header.
  uint64_t size;
  EXPECT_TRUE(files::GetFileSize(kTestLogFilePath, &size));
  EXPECT_EQ(16u, size);

  // Verify file contents
  auto expected = CreateStaticByteBuffer(
      'b', 't', 's', 'n', 'o', 'o', 'p', '\0', 0x00, 0x00, 0x00,
      0x01,                   // version number
      0x00, 0x00, 0x03, 0xE9  // data link type (H1: 1001)
  );
  std::vector<uint8_t> file_contents;
  ASSERT_TRUE(files::ReadFileToVector(kTestLogFilePath, &file_contents));
  EXPECT_TRUE(ContainersEqual(expected, file_contents));
}

TEST_F(BTSnoopLoggerTest, WritePacketAndReset) {
  auto logger = std::make_unique<BTSnoopLogger>();
  EXPECT_TRUE(logger->Initialize(kTestLogFilePath));

  // The file should contain just the header.
  uint64_t size;
  EXPECT_TRUE(files::GetFileSize(kTestLogFilePath, &size));
  EXPECT_EQ(16u, size);

  // Write a packet consisting of 4 bytes.
  auto buffer = CreateStaticByteBuffer('T', 'e', 's', 't');
  EXPECT_TRUE(logger->WritePacket(buffer, false /* is_received */,
                                  false /* is_data */));

  // The record should contain 28 bytes = header (24) + packet (4). With the
  // file header there should be 28 + 16 = 44 bytes.
  EXPECT_TRUE(files::GetFileSize(kTestLogFilePath, &size));
  EXPECT_EQ(44u, size);

  // Verify the file contents. We make two separate comparisons so as to ignore
  // the timestamp record.
  auto expected = CreateStaticByteBuffer(
      'b', 't', 's', 'n', 'o', 'o', 'p', '\0', 0x00, 0x00, 0x00,
      0x01,                    // version number
      0x00, 0x00, 0x03, 0xE9,  // data link type (H1: 1001)

      // Record
      0x00, 0x00, 0x00, 0x04,  // original length ("Test")
      0x00, 0x00, 0x00, 0x04,  // included length ("Test")
      0x00, 0x00, 0x00, 0x02,  // packet flags: sent (0x00) | cmd (0x02)
      0x00, 0x00, 0x00, 0x00   // cumulative drops
  );
  std::vector<uint8_t> file_contents;
  ASSERT_TRUE(files::ReadFileToVector(kTestLogFilePath, &file_contents));
  EXPECT_TRUE(ContainersEqual(expected.begin(), expected.end(),
                              file_contents.begin(),
                              file_contents.begin() + expected.size()));

  // Skip the timestamp and read the packet contents. The timestamp is a 64-bit
  // signed integer and thus 8 bytes long.
  EXPECT_TRUE(ContainersEqual(buffer.begin(), buffer.end(),
                              file_contents.begin() + expected.size() + 8,
                              file_contents.end()));

  // Close the file and re-initialize the logger without truncating. The
  // file contents should be preserved.
  logger = std::make_unique<BTSnoopLogger>();
  EXPECT_TRUE(logger->Initialize(kTestLogFilePath, false /* truncate */));
  EXPECT_TRUE(files::GetFileSize(kTestLogFilePath, &size));
  EXPECT_EQ(44u, size);

  // Close the file and re-initialize the logger. The contents should truncate
  // back to just the header.
  logger = std::make_unique<BTSnoopLogger>();
  EXPECT_TRUE(logger->Initialize(kTestLogFilePath));
  EXPECT_TRUE(files::GetFileSize(kTestLogFilePath, &size));
  EXPECT_EQ(16u, size);
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
