// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/lazy_reader.h"

#include <fcntl.h>
#include <lib/fit/defer.h>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/resizeable_vmo_buffer.h"
#include "src/storage/minfs/writeback.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

class Mapper : public MapperInterface {
 public:
  zx::status<DeviceBlockRange> Map(BlockRange range) {
    auto iter = mappings_.find(range.Start());
    if (iter != mappings_.end()) {
      return zx::ok(DeviceBlockRange(iter->second, 1));
    } else {
      return zx::ok(DeviceBlockRange(DeviceBlock::Unmapped(), 1));
    }
  }

  zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction, BlockRange range,
                                           bool* allocated) {
    EXPECT_FALSE(transaction == nullptr);
    EXPECT_TRUE(*allocated == false);
    auto iter = mappings_.find(range.Start());
    if (iter == mappings_.end()) {
      EXPECT_LT(range.Start(), 10ul);
      // Reverses and spaces every logical block out by 2.
      DeviceBlockRange device_range = DeviceBlockRange(20 - range.Start() * 2, 1);
      mappings_[range.Start()] = device_range.block();
      *allocated = true;
      return zx::ok(device_range);
    } else {
      return zx::ok(DeviceBlockRange(iter->second, 1));
    }
  }

  const std::map<size_t, size_t>& mappings() const { return mappings_; }

 private:
  std::map<size_t, size_t> mappings_;
};

class StubTransaction : public PendingWork {
 public:
  void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) override {}
  void EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) override {}
  size_t AllocateBlock() override { return 0; }
  void DeallocateBlock(size_t) override {}
};

TEST(LazyReaderTest, ReadSucceeds) {
  static const int kBlockCount = 21;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);
  // Write to logical block 1.
  char data[kMinfsBlockSize] = "hello";
  StubTransaction transaction;
  Mapper mapper;
  bool allocated = false;
  DeviceBlockRange device_range =
      mapper.MapForWrite(&transaction, BlockRange(1, 2), &allocated).value();
  bcache->Writeblk(static_cast<blk_t>(device_range.block()), data);

  // Now read the data back using the lazy_reader.
  ResizeableVmoBuffer buffer(kMinfsBlockSize);
  ASSERT_EQ(buffer.Attach("LazyReaderTest", bcache.get()), ZX_OK);
  auto detach = fit::defer([&]() { buffer.Detach(bcache.get()); });
  ASSERT_EQ(buffer.Grow(kBlockCount), ZX_OK);
  MappedFileReader reader(bcache.get(), &mapper, &buffer);
  LazyReader lazy_reader;
  ASSERT_EQ(lazy_reader.Read(ByteRange(kMinfsBlockSize, kMinfsBlockSize + 6), &reader), ZX_OK);

  // We should see the same data read back.
  EXPECT_EQ(memcmp(buffer.Data(1), "hello", 6), 0);
}

TEST(LazyReaderTest, UnmappedBlockIsZeroed) {
  static const int kBlockCount = 21;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);

  ResizeableVmoBuffer buffer(kMinfsBlockSize);
  ASSERT_EQ(buffer.Attach("LazyReaderTest", bcache.get()), ZX_OK);
  auto detach = fit::defer([&]() { buffer.Detach(bcache.get()); });
  *static_cast<uint8_t*>(buffer.Data(0)) = 0xab;
  Mapper mapper;
  MappedFileReader reader(bcache.get(), &mapper, &buffer);
  LazyReader lazy_reader;

  // There is no mapping for the first block so it should get zeroed.
  EXPECT_EQ(lazy_reader.Read(ByteRange(0, 1), &reader), ZX_OK);
  EXPECT_EQ(0, *static_cast<uint8_t*>(buffer.Data(0)));

  // Reading again should not zero it again.
  *static_cast<uint8_t*>(buffer.Data(0)) = 0xab;
  EXPECT_EQ(lazy_reader.Read(ByteRange(0, 1), &reader), ZX_OK);
  EXPECT_EQ(0xab, *static_cast<uint8_t*>(buffer.Data(0)));
}

class MockReader : public LazyReader::ReaderInterface {
 public:
  zx::status<uint64_t> Enqueue(BlockRange range) override {
    if (return_error_for_enqueue_) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }
    enqueued_.push_back(range);
    return zx::ok(range.Length());
  }

  // Issues the queued reads and returns the result.
  zx_status_t RunRequests() override {
    if (return_error_for_run_requests_) {
      return ZX_ERR_IO;
    }
    run_requests_called_ = true;
    return ZX_OK;
  }

  uint32_t BlockSize() const override { return 512; }

  void Reset() {
    enqueued_.clear();
    run_requests_called_ = false;
    return_error_for_enqueue_ = false;
    return_error_for_run_requests_ = false;
  }

  const std::vector<BlockRange>& enqueued() const { return enqueued_; }
  bool run_requests_called() const { return run_requests_called_; }

  void set_return_error_for_enqueue(bool return_error) { return_error_for_enqueue_ = return_error; }
  void set_return_error_for_run_requests(bool return_error) {
    return_error_for_run_requests_ = return_error;
  }

 private:
  std::vector<BlockRange> enqueued_;
  bool run_requests_called_ = false;
  bool return_error_for_enqueue_ = false;
  bool return_error_for_run_requests_ = false;
};

TEST(LazyReaderTest, ZeroLengthReadIsNotEnqeued) {
  LazyReader lazy_reader;
  MockReader reader;

  ASSERT_EQ(lazy_reader.Read(ByteRange(100, 100), &reader), ZX_OK);
  EXPECT_EQ(reader.enqueued().size(), 0ul);
}

TEST(LazyReaderTest, ReadForMutlipleBlocksAfterOneBlockReadEnqueuedCorrectly) {
  LazyReader lazy_reader;
  MockReader reader;

  // Read one block.
  ASSERT_EQ(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_OK);
  ASSERT_EQ(reader.enqueued().size(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 2ul);
  EXPECT_TRUE(reader.run_requests_called());
  reader.Reset();

  // Now read through blocks 0 to 4.
  ASSERT_EQ(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 4 * reader.BlockSize() + 1), &reader),
      ZX_OK);

  // We read one block earlier which shouldn't read again. This should result in Enqueue(0, 1),
  // Enqueue(2, 5).
  ASSERT_EQ(reader.enqueued().size(), 2ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 0ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 1ul);
  EXPECT_EQ(reader.enqueued()[1].Start(), 2ul);
  EXPECT_EQ(reader.enqueued()[1].End(), 5ul);
}

TEST(LazyReaderTest, EnqueueError) {
  LazyReader lazy_reader;
  MockReader reader;
  reader.set_return_error_for_enqueue(true);
  EXPECT_EQ(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_ERR_NO_MEMORY);
  EXPECT_FALSE(reader.run_requests_called());
  reader.Reset();

  // If we try again with no error, it should proceed with the read.
  ASSERT_EQ(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_OK);
  ASSERT_EQ(reader.enqueued().size(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 2ul);
  EXPECT_TRUE(reader.run_requests_called());
}

TEST(LazyReaderTest, RunRequestsError) {
  LazyReader lazy_reader;
  MockReader reader;
  reader.set_return_error_for_run_requests(true);
  EXPECT_EQ(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_ERR_IO);
  EXPECT_FALSE(reader.run_requests_called());
  reader.Reset();

  // If we try again with no error, it should proceed with the read.
  ASSERT_EQ(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_OK);
  ASSERT_EQ(reader.enqueued().size(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 2ul);
  EXPECT_TRUE(reader.run_requests_called());
}

TEST(LazyReaderTest, SetLoadedMarksBlocksAsLoaded) {
  LazyReader lazy_reader;

  lazy_reader.SetLoaded(BlockRange(1, 2), true);

  MockReader reader;
  ASSERT_EQ(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader),
      ZX_OK);
  ASSERT_EQ(reader.enqueued().size(), 2ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 0ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 1ul);
  EXPECT_EQ(reader.enqueued()[1].Start(), 2ul);
  EXPECT_EQ(reader.enqueued()[1].End(), 3ul);
}

TEST(LazyReaderTest, ClearLoadedMarksBlocksAsNotLoaded) {
  LazyReader lazy_reader;
  MockReader reader;
  ASSERT_EQ(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader),
      ZX_OK);

  lazy_reader.SetLoaded(BlockRange(1, 2), false);

  reader.Reset();
  ASSERT_EQ(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader),
      ZX_OK);
  ASSERT_EQ(reader.enqueued().size(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].Start(), 1ul);
  EXPECT_EQ(reader.enqueued()[0].End(), 2ul);
}

}  // namespace
}  // namespace minfs
