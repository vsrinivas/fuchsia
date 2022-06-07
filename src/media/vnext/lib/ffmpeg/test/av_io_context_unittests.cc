// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>

#include <condition_variable>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/ffmpeg/av_io_context.h"

namespace fmlib {
namespace {

constexpr size_t kReaderSize = 65536;
constexpr int kBufferSize = 32 * 1024;
constexpr int kReadBufferSize = 1024;
constexpr int kSeekPosition = 200;

class TestReader : public Reader {
 public:
  // Returns a byte from the virtual file from which this reader reads.
  static uint8_t TestReadData(size_t position) {
    return static_cast<uint8_t>(position ^ (position >> 8) ^ (position >> 16));
  }

  static bool VerifyReadData(uint8_t* buffer, size_t bytes_read, size_t initial_position) {
    size_t pos = initial_position;
    uint8_t* test = buffer;
    for (size_t remaining = bytes_read; remaining != 0; --remaining, ++pos, ++test) {
      EXPECT_EQ(TestReadData(pos), *test);
      if (TestReadData(pos) != *test) {
        return false;
      }
    }

    return true;
  }

  TestReader(zx_status_t status, size_t size, bool can_seek)
      : status_(status), size_(size), can_seek_(can_seek) {}

  ~TestReader() override = default;

  bool VerifyReadAtCalled(size_t position, size_t bytes_read) {
    std::unique_lock<std::mutex> locker(mutex_);
    EXPECT_TRUE(read_at_called_);
    if (!read_at_called_) {
      return false;
    }

    EXPECT_EQ(position, read_at_position_);
    EXPECT_EQ(bytes_read, read_at_bytes_to_read_);

    read_at_called_ = false;

    return (position == read_at_position_) && (bytes_read == read_at_bytes_to_read_);
  }

  // Reader implementation.
  void Describe(DescribeCallback callback) override { callback(status_, size_, can_seek_); }

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override {
    size_t pos = position;
    uint8_t* dest = buffer;
    for (size_t remaining = bytes_to_read; remaining != 0; --remaining, ++pos, ++dest) {
      *dest = TestReadData(pos);
    }

    std::unique_lock<std::mutex> locker(mutex_);
    EXPECT_FALSE(read_at_called_);
    read_at_called_ = true;
    read_at_position_ = position;
    read_at_bytes_to_read_ = bytes_to_read;

    callback(ZX_OK, bytes_to_read);
  }

 private:
  zx_status_t status_;
  size_t size_;
  bool can_seek_;
  std::mutex mutex_;
  bool read_at_called_ = false;
  size_t read_at_position_;
  size_t read_at_bytes_to_read_;
};

class AvIoContextTest : public gtest::RealLoopFixture {
 public:
  AvIoContextTest() : reader_loop_(&kAsyncLoopConfigNeverAttachToThread) {
    reader_loop_.StartThread("AvIoContextTest");
  }

  // Returns the dispatcher associated with |reader_loop_|.
  async_dispatcher_t* dispatcher() const { return reader_loop_.dispatcher(); }

  // Creates a reader on the thread associated with |reader_loop_|.
  std::shared_ptr<TestReader> CreateReader(zx_status_t status, size_t size, bool can_seek) {
    std::shared_ptr<TestReader> reader;
    std::mutex mutex;
    std::condition_variable cv;

    async::PostTask(reader_loop_.dispatcher(), [status, size, can_seek, &reader, &mutex, &cv]() {
      std::lock_guard<std::mutex> locker(mutex);
      reader = std::make_shared<TestReader>(status, size, can_seek);
      cv.notify_all();
    });

    std::unique_lock<std::mutex> locker(mutex);
    while (!reader) {
      cv.wait(locker);
    }

    return reader;
  }

 private:
  async::Loop reader_loop_;
};

// Tests the |AvIoContext::Create| static method for seekable readers.
TEST_F(AvIoContextTest, CreateSeekable) {
  auto reader = std::make_shared<TestReader>(ZX_OK, kReaderSize, /* can_seek */ true);
  AvIoContextPtr av_io_context_ptr;
  auto result = AvIoContext::Create(reader, dispatcher());
  EXPECT_TRUE(result.is_ok());
  auto under_test = result.take_value();
  EXPECT_TRUE(!!under_test);
  EXPECT_TRUE(under_test->seekable);
  EXPECT_EQ(0, under_test->write_flag);
  EXPECT_TRUE(!!under_test->buffer);
  EXPECT_EQ(kBufferSize, under_test->buffer_size);
  EXPECT_FALSE(under_test->eof_reached);
}

// Tests the |AvIoContext::Create| static method for unseekable readers.
TEST_F(AvIoContextTest, CreateUnseekable) {
  auto reader = std::make_shared<TestReader>(ZX_OK, kReaderSize, /* can_seek */ false);
  AvIoContextPtr av_io_context_ptr;
  auto result = AvIoContext::Create(reader, dispatcher());
  EXPECT_TRUE(result.is_ok());
  auto under_test = result.take_value();
  EXPECT_TRUE(!!under_test);
  EXPECT_FALSE(under_test->seekable);
  EXPECT_EQ(0, under_test->write_flag);
  EXPECT_TRUE(!!under_test->buffer);
  EXPECT_EQ(kBufferSize, under_test->buffer_size);
  EXPECT_FALSE(under_test->eof_reached);
}

// Tests the static read and seek methods.
TEST_F(AvIoContextTest, ReadAndSeek) {
  auto reader = std::make_shared<TestReader>(ZX_OK, kReaderSize, /* can_seek */ true);
  AvIoContextPtr av_io_context_ptr;
  auto result = AvIoContext::Create(reader, dispatcher());
  EXPECT_TRUE(result.is_ok());
  auto under_test = result.take_value();

  uint8_t buffer[kReadBufferSize];
  int read_result = under_test->read_packet(under_test->opaque, buffer, kReadBufferSize);
  EXPECT_EQ(kReadBufferSize, read_result);
  EXPECT_TRUE(reader->VerifyReadAtCalled(0, kReadBufferSize));
  EXPECT_TRUE(TestReader::VerifyReadData(buffer, kReadBufferSize, 0));
  EXPECT_EQ(kReadBufferSize, under_test->seek(under_test->opaque, 0, SEEK_CUR));
  EXPECT_FALSE(under_test->eof_reached);

  read_result = under_test->read_packet(under_test->opaque, buffer, kReadBufferSize);
  EXPECT_EQ(kReadBufferSize, read_result);
  EXPECT_TRUE(reader->VerifyReadAtCalled(kReadBufferSize, kReadBufferSize));
  EXPECT_TRUE(TestReader::VerifyReadData(buffer, kReadBufferSize, kReadBufferSize));
  EXPECT_EQ(2 * kReadBufferSize, under_test->seek(under_test->opaque, 0, SEEK_CUR));
  EXPECT_FALSE(under_test->eof_reached);

  EXPECT_EQ(kSeekPosition, under_test->seek(under_test->opaque, kSeekPosition, SEEK_SET));
  read_result = under_test->read_packet(under_test->opaque, buffer, kReadBufferSize);
  EXPECT_EQ(kReadBufferSize, read_result);
  EXPECT_TRUE(reader->VerifyReadAtCalled(kSeekPosition, kReadBufferSize));
  EXPECT_TRUE(TestReader::VerifyReadData(buffer, kReadBufferSize, kSeekPosition));
  EXPECT_EQ(kSeekPosition + kReadBufferSize, under_test->seek(under_test->opaque, 0, SEEK_CUR));
  EXPECT_FALSE(under_test->eof_reached);

  EXPECT_EQ(kReadBufferSize, under_test->seek(under_test->opaque, -kSeekPosition, SEEK_CUR));
  read_result = under_test->read_packet(under_test->opaque, buffer, kReadBufferSize);
  EXPECT_EQ(kReadBufferSize, read_result);
  EXPECT_TRUE(reader->VerifyReadAtCalled(kReadBufferSize, kReadBufferSize));
  EXPECT_TRUE(TestReader::VerifyReadData(buffer, kReadBufferSize, kReadBufferSize));
  EXPECT_EQ(2 * kReadBufferSize, under_test->seek(under_test->opaque, 0, SEEK_CUR));
  EXPECT_FALSE(under_test->eof_reached);

  EXPECT_EQ(kReaderSize - kReadBufferSize,
            static_cast<size_t>(under_test->seek(under_test->opaque, -kReadBufferSize, SEEK_END)));
  read_result = under_test->read_packet(under_test->opaque, buffer, kReadBufferSize);
  EXPECT_EQ(kReadBufferSize, read_result);
  EXPECT_TRUE(reader->VerifyReadAtCalled(kReaderSize - kReadBufferSize, kReadBufferSize));
  EXPECT_TRUE(TestReader::VerifyReadData(buffer, kReadBufferSize, kReaderSize - kReadBufferSize));
  EXPECT_EQ(kReaderSize, static_cast<size_t>(under_test->seek(under_test->opaque, 0, SEEK_CUR)));
  EXPECT_FALSE(under_test->eof_reached);

  EXPECT_EQ(kReaderSize, static_cast<size_t>(under_test->seek(under_test->opaque, 0, AVSEEK_SIZE)));
}

}  // namespace
}  // namespace fmlib
