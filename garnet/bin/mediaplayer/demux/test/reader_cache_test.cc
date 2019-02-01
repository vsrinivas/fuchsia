// // Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/reader_cache.h"

#include "gtest/gtest.h"

namespace media_player {
namespace {

class FakeReader : public Reader {
 public:
  FakeReader() {}

  void Describe(DescribeCallback callback) override {
    callback(Result::kOk, 500000, true);
  }

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override {
    callback_ = std::move(callback);
    bytes_to_read_ = bytes_to_read;
  }

  std::pair<ReadAtCallback, size_t> GetCallback() {
      return {std::move(callback_), bytes_to_read_};
  }

 private:
  ReadAtCallback callback_;
  size_t bytes_to_read_;
};

TEST(ReaderCache, MTWN214Repro) {
  auto fake_reader = std::make_shared<FakeReader>();
  auto under_test = ReaderCache::Create(fake_reader);
  uint8_t dest[800] = {0};

  // Set up a load and leave it hanging.
  under_test->ReadAt(0, dest, 100, [](Result result, size_t bytes_read) {});
  auto [first_callback, first_bytes] = fake_reader->GetCallback();

  // Start a new load so that ReadAt queues a recursive call on the upstream
  // reader callback incident.
  under_test->ReadAt(101, dest, 300, [](Result result, size_t bytes_read) {});
  under_test->ReadAt(300, dest, 600, [](Result result, size_t bytes_read) {});

  // Finish the first load, so that the reader callback incident calls itself. It
  // will not escape before hitting the stack limit because we aren't finishing
  // any more loads in this test.
  //
  // To pass, this just needs to not crash.
  first_callback(Result::kOk, 100);
}

}  // namespace
}  // namespace media_player
