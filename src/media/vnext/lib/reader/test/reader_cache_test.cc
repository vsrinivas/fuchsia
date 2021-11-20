// // Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/reader/reader_cache.h"

#include <lib/async/cpp/executor.h>
#include <lib/gtest/real_loop_fixture.h>

#include <cmath>
#include <cstdlib>

namespace fmlib {
namespace {

class FakeReader : public Reader {
 public:
  struct ReadAtRequest {
    ReadAtCallback callback;
    size_t position;
    uint8_t* buffer;
    size_t bytes_to_read;
  };

  FakeReader() = default;

  void Describe(DescribeCallback callback) override { describe_callback_ = std::move(callback); }

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override {
    ASSERT_NE(callback, nullptr);
    request_ = {.callback = std::move(callback),
                .position = position,
                .buffer = buffer,
                .bytes_to_read = bytes_to_read};
  }

  std::optional<ReadAtRequest> GetReadAtRequest() {
    auto request = std::move(request_);
    request_ = std::nullopt;
    return request;
  }

  DescribeCallback GetDescribeCallback() { return std::move(describe_callback_); }

 private:
  std::optional<ReadAtRequest> request_;
  DescribeCallback describe_callback_;
};

class ReaderCacheTest : public gtest::RealLoopFixture {
 public:
  ReaderCacheTest() : executor_(dispatcher()) {}

 protected:
  async::Executor& executor() { return executor_; }

 private:
  async::Executor executor_;
};

TEST_F(ReaderCacheTest, MTWN214Repro) {
  auto fake_reader = std::make_shared<FakeReader>();
  auto under_test = ReaderCache::Create(executor(), fake_reader);
  fake_reader->GetDescribeCallback()(ZX_OK, 500000, true);

  uint8_t dest[800] = {0};

  // Set up a load and leave it hanging.
  under_test->ReadAt(0, dest, 100, [](zx_status_t status, size_t bytes_read) {});
  RunLoopUntilIdle();

  auto request = fake_reader->GetReadAtRequest();
  EXPECT_NE(request, std::nullopt);

  // Start a new load so that ReadAt queues a recursive call on the upstream
  // reader callback incident.
  under_test->ReadAt(101, dest, 300, [](zx_status_t status, size_t bytes_read) {});
  under_test->ReadAt(300, dest, 600, [](zx_status_t status, size_t bytes_read) {});
  RunLoopUntilIdle();

  // Finish the first load, so that the reader callback incident calls itself.
  // It will not escape before hitting the stack limit because we aren't
  // finishing any more loads in this test.
  //
  // To pass, this just needs to not crash.
  EXPECT_TRUE(!!request->callback);
  request->callback(ZX_OK, request->bytes_to_read);
}

TEST_F(ReaderCacheTest, SunnyDayAPI) {
  auto fake_reader = std::make_shared<FakeReader>();
  auto under_test = ReaderCache::Create(executor(), fake_reader);

  constexpr size_t kCapacity = 100;
  constexpr size_t kBacktrack = 10;
  under_test->SetCacheOptions(kCapacity, kBacktrack);

  constexpr size_t kSourceSize = 1000;
  uint8_t source[kSourceSize] = {0};
  for (size_t i = 0; i < kSourceSize; ++i) {
    source[i] = static_cast<uint8_t>(i & 0xff);
  }
  fake_reader->GetDescribeCallback()(ZX_OK, kSourceSize, true);

  const size_t seeks = 200;
  srand(12929);
  for (size_t i = 0; i < seeks; ++i) {
    // This will create some reads of greater size than the cache storage,
    // forcing it to try multiple loads to make forward progress.
    const size_t seek_size = rand() % kCapacity + 1;

    // This range is allowed to select seek start points that are too near the
    // end of the upstream source to be fully serviced (e.g. a read of 10 bytes
    // at the 8th byte in a 10 byte medium).
    const size_t seek_start = rand() % kSourceSize;
    const size_t expected_bytes_read = std::min(kSourceSize - seek_start, seek_size);

    std::vector<uint8_t> buffer(seek_size, 0);

    bool callback_executed = false;
    under_test->ReadAt(
        seek_start, &buffer[0], seek_size,
        [&callback_executed, expected_bytes_read](zx_status_t status, size_t bytes_read) {
          EXPECT_EQ(bytes_read, expected_bytes_read);
          EXPECT_EQ(status, ZX_OK);
          callback_executed = true;
        });

    RunLoopUntilIdle();
    std::optional<FakeReader::ReadAtRequest> request;
    while ((request = fake_reader->GetReadAtRequest())) {
      EXPECT_NE(request->buffer, nullptr);
      EXPECT_NE(request->callback, nullptr);
      memcpy(request->buffer, &source[request->position], request->bytes_to_read);
      request->callback(ZX_OK, request->bytes_to_read);
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(callback_executed);

    EXPECT_EQ(memcmp(&buffer[0], &source[seek_start], expected_bytes_read), 0);
  }
}

TEST_F(ReaderCacheTest, ReportFailure) {
  auto fake_reader = std::make_shared<FakeReader>();
  auto under_test = ReaderCache::Create(executor(), fake_reader);

  constexpr size_t kCapacity = 100;
  constexpr size_t kBacktrack = 10;
  under_test->SetCacheOptions(kCapacity, kBacktrack);

  constexpr size_t kSourceSize = 1000;
  fake_reader->GetDescribeCallback()(ZX_OK, kSourceSize, true);

  std::vector<uint8_t> buffer(10, 0);
  bool callback_executed = false;
  under_test->ReadAt(0, &buffer[0], 10,
                     [&callback_executed](zx_status_t status, size_t bytes_read) {
                       EXPECT_EQ(status, ZX_ERR_INTERNAL);
                       callback_executed = true;
                     });

  RunLoopUntilIdle();
  auto request = fake_reader->GetReadAtRequest();
  EXPECT_NE(request, std::nullopt);
  request->callback(ZX_ERR_INTERNAL, 0);

  EXPECT_TRUE(callback_executed);
}

}  // namespace
}  // namespace fmlib
