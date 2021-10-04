// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/bounded_writer.h"

#include <lib/stdcompat/array.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace storage::volume_image {
namespace {

class FakeWriter final : public Writer {
 public:
  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset + buffer.size() > data_.size()) {
      data_.resize(offset + buffer.size(), 0);
    }
    memcpy(data_.data() + offset, buffer.data(), buffer.size());
    return fpromise::ok();
  }

  auto& data() { return data_; }

 private:
  std::vector<uint8_t> data_;
};

constexpr auto kData = cpp20::to_array<uint8_t>({1, 2, 3});

TEST(BoundedWriterTest, WriteOutOfBoundsIsError) {
  std::unique_ptr<FakeWriter> writer = std::make_unique<FakeWriter>();
  auto& data = writer->data();
  BoundedWriter bounded_writer(std::move(writer), 123, 123);

  // Write past end
  EXPECT_TRUE(bounded_writer.Write(121, kData).is_error());
  EXPECT_TRUE(data.size() == 0);
}

TEST(BoundedWriterTest, WriteWithinBoundsIsOk) {
  std::unique_ptr<FakeWriter> writer = std::make_unique<FakeWriter>();
  auto& data = writer->data();
  BoundedWriter bounded_writer(std::move(writer), 123, 123);

  ASSERT_TRUE(bounded_writer.Write(120, kData).is_ok());
  ASSERT_TRUE(data.size() == 246);
  EXPECT_EQ(data[243], 1);
  EXPECT_EQ(data[244], 2);
  EXPECT_EQ(data[245], 3);
}

}  // namespace
}  // namespace storage::volume_image
