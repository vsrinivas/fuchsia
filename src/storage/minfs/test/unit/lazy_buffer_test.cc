// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/lazy_buffer.h"

#include <lib/stdcompat/span.h>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/resizeable_vmo_buffer.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;

// Offset chosen such that we write to two logical blocks.
constexpr unsigned kOffset = kMinfsBlockSize * 3 - 4;
// We treat the buffer as an array of uint32_t.
constexpr unsigned kIndex = kOffset / 4;

// SimpleMapper multiplies the file block by 2.
class SimpleMapper : public MapperInterface {
 public:
  zx::status<DeviceBlockRange> Map(BlockRange range) {
    return zx::ok(DeviceBlockRange(range.Start() * 2, 1));
  }

  zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction, BlockRange range,
                                           bool* allocated) {
    *allocated = false;
    return Map(range);
  }
};

class LazyBufferTest : public testing::Test {
 public:
  static constexpr int kNumBlocks = 20;

  void SetUp() override {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kMinfsBlockSize);
    ASSERT_TRUE(device);

    auto bcache_or = Bcache::Create(std::move(device), kNumBlocks);
    ASSERT_TRUE(bcache_or.is_ok());
    bcache_ = std::move(bcache_or.value());

    ResetBuffer();
  }

  ~LazyBufferTest() {
    if (buffer_)
      EXPECT_TRUE(buffer_->Detach(bcache_.get()).is_ok());
  }

  // Writes |data| at kIndex.
  void Write(cpp20::span<const uint32_t> data) {
    SimpleMapper mapper;
    auto flusher = [this, &mapper](BaseBufferView* view) {
      return buffer_->Flush(
          nullptr, &mapper, view,
          [this](ResizeableVmoBuffer* buffer, BlockRange range, DeviceBlock device_block) {
            return zx::make_status(
                bcache_->RunOperation(storage::Operation{.type = storage::OperationType::kWrite,
                                                         .vmo_offset = range.Start(),
                                                         .dev_offset = device_block.block(),
                                                         .length = 1},
                                      buffer));
          });
    };
    LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
    BufferView<uint32_t> view =
        buffer_->GetView<uint32_t>(kIndex, data.size(), &reader, std::move(flusher)).value();
    for (size_t i = 0; i < data.size(); ++i) {
      view.mut_ref(i) = data[i];
    }
    ASSERT_TRUE(view.Flush().is_ok());
  }

  void ResetBuffer() {
    if (buffer_)
      EXPECT_TRUE(buffer_->Detach(bcache_.get()).is_ok());
    buffer_ = LazyBuffer::Create(bcache_.get(), "LazyBufferTest", kMinfsBlockSize).value();
  }

 protected:
  std::unique_ptr<Bcache> bcache_;
  std::unique_ptr<LazyBuffer> buffer_;
};

TEST_F(LazyBufferTest, ReadSucceeds) {
  constexpr std::array<uint32_t, 2> kData = {37, 54};
  Write(kData);

  ResetBuffer();
  SimpleMapper mapper;
  LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
  BufferView<uint32_t> view = buffer_->GetView<uint32_t>(kIndex, 2, &reader).value();

  for (size_t i = 0; i < kData.size(); ++i) {
    EXPECT_EQ(kData[i], view[i]);
  }
}

TEST_F(LazyBufferTest, ShrinkShrinksBuffer) {
  constexpr std::array<uint32_t, 2> kData = {1, 2};
  Write(kData);

  buffer_->Shrink(1);
  EXPECT_EQ(kMinfsBlockSize, buffer_->size());
}

TEST_F(LazyBufferTest, ShrinkToZeroBlocksShrinksToMinimum) {
  constexpr std::array<uint32_t, 2> kData = {1, 2};
  Write(kData);

  buffer_->Shrink(0);

  EXPECT_EQ(kMinfsBlockSize, buffer_->size());
}

TEST_F(LazyBufferTest, ShrinkDoesNotGrowIfAlreadySmaller) {
  constexpr std::array<uint32_t, 2> kData = {1, 2};
  Write(kData);
  EXPECT_EQ(fbl::round_up(kOffset + 8, kMinfsBlockSize), buffer_->size());

  buffer_->Shrink(kIndex + 3);

  EXPECT_EQ(fbl::round_up(kOffset + 8, kMinfsBlockSize), buffer_->size());
}

TEST_F(LazyBufferTest, ShrinkClearsLoaded) {
  SimpleMapper mapper;
  LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
  // This should cause a block to be loaded.
  BufferView<uint32_t> view = buffer_->GetView<uint32_t>(0, 1, &reader).value();

  buffer_->Shrink(0);

  // To test that loaded was cleared, write to the buffer directly and then see that it can be read
  // back.
  storage::VmoBuffer temp_buffer;
  ASSERT_EQ(temp_buffer.Initialize(bcache_->device(), 1, kMinfsBlockSize, "temp"), ZX_OK);
  constexpr uint8_t kData = 0xaf;
  memset(temp_buffer.Data(0), kData, 1);
  EXPECT_EQ(bcache_->RunOperation(storage::Operation{.type = storage::OperationType::kWrite,
                                                     .vmo_offset = 0,
                                                     .dev_offset = 0,
                                                     .length = 1},
                                  &temp_buffer),
            ZX_OK);
  view = buffer_->GetView<uint32_t>(0, 1, &reader).value();
  EXPECT_EQ(kData, *view);
}

TEST_F(LazyBufferTest, FlushWritesAllBlocksInRange) {
  // View spans 5 whole blocks after alignment.
  constexpr int kViewBlockCount = 5;
  SimpleMapper mapper;
  LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
  std::vector<std::pair<BlockRange, DeviceBlock>> write_calls;
  // Arrange for the span to touch kViewBlockCount blocks.
  BufferView<uint32_t> view =
      buffer_
          ->GetView<uint32_t>(
              kMinfsBlockSize / 4 - 1, kMinfsBlockSize / 4 * (kViewBlockCount - 2) + 2, &reader,
              [&](BaseBufferView* view) {
                return buffer_->Flush(/*transaction*/ nullptr, &mapper, view,
                                      [&](ResizeableBufferType* resizeable_buffer, BlockRange range,
                                          DeviceBlock device_block) {
                                        EXPECT_EQ(&buffer_->buffer(), resizeable_buffer);
                                        write_calls.push_back(std::make_pair(range, device_block));
                                        return zx::ok();
                                      });
              })
          .value();
  view.mut_ref(0) = 1;

  EXPECT_TRUE(view.Flush().is_ok());

  ASSERT_EQ(write_calls.size(), size_t{kViewBlockCount});
  for (int i = 0; i < kViewBlockCount; ++i) {
    EXPECT_EQ(BlockRange(i, i + 1), write_calls[i].first);
    EXPECT_EQ(mapper.Map(write_calls[i].first)->device_block(), write_calls[i].second);
  }
}

class ErrorMapper : public MapperInterface {
 public:
  zx::status<DeviceBlockRange> Map(BlockRange range) { return zx::error(ZX_ERR_IO); }

  zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction, BlockRange range,
                                           bool* allocated) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
};

TEST_F(LazyBufferTest, GetViewReturnsAnErrorWhenReadFails) {
  ErrorMapper error_mapper;
  LazyBuffer::Reader reader(bcache_.get(), &error_mapper, buffer_.get());

  EXPECT_EQ(buffer_->GetView<uint32_t>(0, 1, &reader).status_value(), ZX_ERR_IO);
}

TEST_F(LazyBufferTest, FlushReturnsErrorWhenMapperFails) {
  SimpleMapper mapper;
  LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
  BufferView<uint32_t> view =
      buffer_
          ->GetView<uint32_t>(0, 1, &reader,
                              [&](BaseBufferView* view) {
                                ErrorMapper error_mapper;
                                return buffer_->Flush(
                                    /*transaction*/ nullptr, &error_mapper, view,
                                    [&](ResizeableBufferType* resizeable_buffer, BlockRange range,
                                        DeviceBlock device_block) {
                                      ADD_FAILURE() << "Writer shouldn't be called.";
                                      return zx::ok();
                                    });
                              })
          .value();
  view.mut_ref(0) = 1;

  EXPECT_EQ(view.Flush().status_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(LazyBufferTest, FlushReturnsErrorWhenWriteFails) {
  SimpleMapper mapper;
  LazyBuffer::Reader reader(bcache_.get(), &mapper, buffer_.get());
  BufferView<uint32_t> view =
      buffer_
          ->GetView<uint32_t>(0, 1, &reader,
                              [&](BaseBufferView* view) {
                                return buffer_->Flush(
                                    /*transaction*/ nullptr, &mapper, view,
                                    [&](ResizeableBufferType* resizeable_buffer, BlockRange range,
                                        DeviceBlock device_block) { return zx::error(ZX_ERR_IO); });
                              })
          .value();
  view.mut_ref(0) = 1;

  EXPECT_EQ(view.Flush().status_value(), ZX_ERR_IO);
}

}  // namespace
}  // namespace minfs
