// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>

#include <ddktl/device.h>
#include <gtest/gtest.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_auto_reader.h"
#include "src/devices/testing/goldfish/fake_pipe/fake_pipe.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace goldfish::sensor {
namespace {

class PipeIoTest : public ::testing::Test {
 public:
  void SetUp() override {
    pipe_client_ = ddk::GoldfishPipeProtocolClient(pipe_.proto());
    io_ = std::make_unique<PipeIo>(&pipe_client_, "pipe");
  }

  void TearDown() override {}

 protected:
  testing::FakePipe pipe_;
  ddk::GoldfishPipeProtocolClient pipe_client_;
  std::unique_ptr<PipeIo> io_;
};

TEST_F(PipeIoTest, BlockingWrite) {
  auto old_size = pipe_.io_buffer_contents().size();

  const char* kSrcString1 = "hello world";
  io_->Write(kSrcString1, true);
  ASSERT_EQ(pipe_.io_buffer_contents().size(), old_size + 1);
  EXPECT_EQ(memcmp(pipe_.io_buffer_contents().back().data(), kSrcString1, strlen(kSrcString1)), 0);

  const char* kDstString1 = "000bhello world";
  io_->WriteWithHeader(kSrcString1);
  ASSERT_EQ(pipe_.io_buffer_contents().size(), old_size + 2);
  EXPECT_EQ(memcmp(pipe_.io_buffer_contents().back().data(), kDstString1, strlen(kDstString1)), 0);
}

TEST_F(PipeIoTest, NonBlockingRead) {
  const char* kSrcString1 = "hello world";

  // If the pipe has some bytes available for reading, we'll be able to read it
  // directly.
  {
    std::vector<uint8_t> bytes_to_read(kSrcString1, kSrcString1 + strlen(kSrcString1));
    pipe_.EnqueueBytesToRead(bytes_to_read);

    auto read_result = io_->Read(strlen(kSrcString1), false);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(memcmp(read_result.value().data(), bytes_to_read.data(), bytes_to_read.size()), 0);
    // Read result has trailing zero.
    EXPECT_EQ(read_result.value().size(), bytes_to_read.size() + 1);
    EXPECT_EQ(read_result.value().back(), 0u);
  }

  // Test reading strings with frame headers.
  const char* kHeader = "000b";
  {
    std::vector<uint8_t> header(kHeader, kHeader + strlen(kHeader));
    pipe_.EnqueueBytesToRead(header);

    std::vector<uint8_t> bytes_to_read(kSrcString1, kSrcString1 + strlen(kSrcString1));
    pipe_.EnqueueBytesToRead(bytes_to_read);

    auto read_result = io_->ReadWithHeader(false);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(memcmp(read_result.value().data(), bytes_to_read.data(), bytes_to_read.size()), 0);
    // Read result has trailing zero.
    EXPECT_EQ(read_result.value().size(), bytes_to_read.size() + 1);
    EXPECT_EQ(read_result.value().back(), 0u);
  }

  // If the pipe doesn't have anything to read, it will return a
  // PIPE_ERROR_AGAIN error.
  {
    auto read_result = io_->Read(strlen(kSrcString1), false);
    ASSERT_TRUE(read_result.is_error());
    EXPECT_EQ(read_result.error(), ZX_ERR_SHOULD_WAIT);
  }

  // If the pipe has fewer bytes than requested, it will return a
  // PIPE_ERROR_AGAIN error as well.
  {
    std::vector<uint8_t> bytes_to_read(kSrcString1, kSrcString1 + strlen(kSrcString1));
    pipe_.EnqueueBytesToRead(bytes_to_read);

    auto read_result = io_->Read(strlen(kSrcString1) * 2, false);
    ASSERT_TRUE(read_result.is_error());
    EXPECT_EQ(read_result.error(), ZX_ERR_SHOULD_WAIT);
  }
}

TEST_F(PipeIoTest, BlockingRead) {
  bool read = false;
  bool contents_correct = false;
  const char* kSegment1 = "hello";
  const char* kSegment2 = "world!";
  const char* kConcat = "helloworld!";

  std::thread t([this, kConcat, &read, &contents_correct] {
    auto result = io_->Read(strlen(kConcat), true);
    read = true;

    ASSERT_TRUE(result.is_ok());
    contents_correct = memcmp(result.value().data(), kConcat, strlen(kConcat)) == 0;
  });

  std::vector<uint8_t> bytes_to_read(kSegment1, kSegment1 + strlen(kSegment1));
  pipe_.EnqueueBytesToRead(bytes_to_read);

  ASSERT_FALSE(read);

  bytes_to_read = std::vector<uint8_t>(kSegment2, kSegment2 + strlen(kSegment2));
  pipe_.EnqueueBytesToRead(bytes_to_read);

  ASSERT_FALSE(read);

  pipe_.pipe_event().signal(0u, fuchsia_hardware_goldfish::wire::kSignalReadable);
  t.join();

  ASSERT_TRUE(read);
  ASSERT_TRUE(contents_correct);
}

class PipeAutoReaderTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    pipe_client_ = ddk::GoldfishPipeProtocolClient(pipe_.proto());
  }

  void TearDown() override { TestLoopFixture::TearDown(); }

 protected:
  testing::FakePipe pipe_;
  ddk::GoldfishPipeProtocolClient pipe_client_;
};

TEST_F(PipeAutoReaderTest, AutoRead) {
  int sum = 0;
  std::unique_ptr<PipeAutoReader> reader = std::make_unique<PipeAutoReader>(
      &pipe_client_, "pipe", dispatcher(), [&sum](PipeIo::ReadResult result) {
        ASSERT_TRUE(result.is_ok());
        int val = 0;
        sscanf(reinterpret_cast<const char*>(result.value().data()), "%d", &val);
        sum += val;
      });
  reader->BeginRead();
  EXPECT_TRUE(RunLoopUntilIdle());

  {
    const char* kHeader = "0003";
    const char* kNum = "123";
    std::vector<uint8_t> header(kHeader, kHeader + strlen(kHeader));
    std::vector<uint8_t> bytes_to_read(kNum, kNum + strlen(kNum));
    pipe_.EnqueueBytesToRead(header);
    pipe_.EnqueueBytesToRead(bytes_to_read);
    pipe_.pipe_event().signal(0u, fuchsia_hardware_goldfish::wire::kSignalReadable);
  }
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(sum, 123);

  {
    const char* kHeader = "0004";
    const char* kNum = "4567";
    std::vector<uint8_t> header(kHeader, kHeader + strlen(kHeader));
    std::vector<uint8_t> bytes_to_read(kNum, kNum + strlen(kNum));
    pipe_.EnqueueBytesToRead(header);
    pipe_.EnqueueBytesToRead(bytes_to_read);
    pipe_.pipe_event().signal(0u, fuchsia_hardware_goldfish::wire::kSignalReadable);
  }
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(sum, 123 + 4567);

  {
    const char* kHeader = "0005";
    const char* kNum = "89012";
    std::vector<uint8_t> header(kHeader, kHeader + strlen(kHeader));
    std::vector<uint8_t> bytes_to_read(kNum, kNum + strlen(kNum));
    pipe_.EnqueueBytesToRead(header);
    pipe_.EnqueueBytesToRead(bytes_to_read);
    pipe_.pipe_event().signal(0u, fuchsia_hardware_goldfish::wire::kSignalReadable);
  }
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(sum, 123 + 4567 + 89012);

  reader->StopRead();
}

}  // namespace
}  // namespace goldfish::sensor
