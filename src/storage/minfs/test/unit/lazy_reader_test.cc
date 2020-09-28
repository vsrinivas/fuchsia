// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/lazy_reader.h"

#include <fcntl.h>

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

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
      EXPECT_GE(10, range.Start());
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
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
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
  ASSERT_OK(buffer.Attach("LazyReaderTest", bcache.get()));
  auto detach = fbl::MakeAutoCall([&]() { buffer.Detach(bcache.get()); });
  ASSERT_OK(buffer.Grow(kBlockCount));
  MappedFileReader reader(bcache.get(), &mapper, &buffer);
  LazyReader lazy_reader;
  ASSERT_OK(lazy_reader.Read(ByteRange(kMinfsBlockSize, kMinfsBlockSize + 6), &reader));

  // We should see the same data read back.
  EXPECT_BYTES_EQ(buffer.Data(1), "hello", 6);
}

TEST(LazyReaderTest, UnmappedBlockIsZeroed) {
  static const int kBlockCount = 21;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));

  ResizeableVmoBuffer buffer(kMinfsBlockSize);
  ASSERT_OK(buffer.Attach("LazyReaderTest", bcache.get()));
  auto detach = fbl::MakeAutoCall([&]() { buffer.Detach(bcache.get()); });
  *static_cast<uint8_t*>(buffer.Data(0)) = 0xab;
  Mapper mapper;
  MappedFileReader reader(bcache.get(), &mapper, &buffer);
  LazyReader lazy_reader;

  // There is no mapping for the first block so it should get zeroed.
  EXPECT_OK(lazy_reader.Read(ByteRange(0, 1), &reader));
  EXPECT_EQ(0, *static_cast<uint8_t*>(buffer.Data(0)));

  // Reading again should not zero it again.
  *static_cast<uint8_t*>(buffer.Data(0)) = 0xab;
  EXPECT_OK(lazy_reader.Read(ByteRange(0, 1), &reader));
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

  ASSERT_OK(lazy_reader.Read(ByteRange(100, 100), &reader));
  EXPECT_EQ(0, reader.enqueued().size());
}

TEST(LazyReaderTest, ReadForMutlipleBlocksAfterOneBlockReadEnqueuedCorrectly) {
  LazyReader lazy_reader;
  MockReader reader;

  // Read one block.
  ASSERT_OK(lazy_reader.Read(ByteRange(530, 531), &reader));
  ASSERT_EQ(1, reader.enqueued().size());
  EXPECT_EQ(1, reader.enqueued()[0].Start());
  EXPECT_EQ(2, reader.enqueued()[0].End());
  EXPECT_TRUE(reader.run_requests_called());
  reader.Reset();

  // Now read through blocks 0 to 4.
  ASSERT_OK(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 4 * reader.BlockSize() + 1), &reader));

  // We read one block earlier which shouldn't read again. This should result in Enqueue(0, 1),
  // Enqueue(2, 5).
  ASSERT_EQ(2, reader.enqueued().size());
  EXPECT_EQ(0, reader.enqueued()[0].Start());
  EXPECT_EQ(1, reader.enqueued()[0].End());
  EXPECT_EQ(2, reader.enqueued()[1].Start());
  EXPECT_EQ(5, reader.enqueued()[1].End());
}

TEST(LazyReaderTest, EnqueueError) {
  LazyReader lazy_reader;
  MockReader reader;
  reader.set_return_error_for_enqueue(true);
  EXPECT_STATUS(lazy_reader.Read(ByteRange(530, 531), &reader), ZX_ERR_NO_MEMORY);
  EXPECT_FALSE(reader.run_requests_called());
  reader.Reset();

  // If we try again with no error, it should proceed with the read.
  ASSERT_OK(lazy_reader.Read(ByteRange(530, 531), &reader));
  ASSERT_EQ(1, reader.enqueued().size());
  EXPECT_EQ(1, reader.enqueued()[0].Start());
  EXPECT_EQ(2, reader.enqueued()[0].End());
  EXPECT_TRUE(reader.run_requests_called());
}

TEST(LazyReaderTest, RunRequestsError) {
  LazyReader lazy_reader;
  MockReader reader;
  reader.set_return_error_for_run_requests(true);
  EXPECT_STATUS(ZX_ERR_IO, lazy_reader.Read(ByteRange(530, 531), &reader));
  EXPECT_FALSE(reader.run_requests_called());
  reader.Reset();

  // If we try again with no error, it should proceed with the read.
  ASSERT_OK(lazy_reader.Read(ByteRange(530, 531), &reader));
  ASSERT_EQ(1, reader.enqueued().size());
  EXPECT_EQ(1, reader.enqueued()[0].Start());
  EXPECT_EQ(2, reader.enqueued()[0].End());
  EXPECT_TRUE(reader.run_requests_called());
}

TEST(LazyReaderTest, SetLoadedMarksBlocksAsLoaded) {
  LazyReader lazy_reader;

  lazy_reader.SetLoaded(BlockRange(1, 2), true);

  MockReader reader;
  ASSERT_OK(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader));
  ASSERT_EQ(2, reader.enqueued().size());
  EXPECT_EQ(0, reader.enqueued()[0].Start());
  EXPECT_EQ(1, reader.enqueued()[0].End());
  EXPECT_EQ(2, reader.enqueued()[1].Start());
  EXPECT_EQ(3, reader.enqueued()[1].End());
}

TEST(LazyReaderTest, ClearLoadedMarksBlocksAsNotLoaded) {
  LazyReader lazy_reader;
  MockReader reader;
  ASSERT_OK(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader));

  lazy_reader.SetLoaded(BlockRange(1, 2), false);

  reader.Reset();
  ASSERT_OK(
      lazy_reader.Read(ByteRange(reader.BlockSize() - 1, 2 * reader.BlockSize() + 1), &reader));
  ASSERT_EQ(1, reader.enqueued().size());
  EXPECT_EQ(1, reader.enqueued()[0].Start());
  EXPECT_EQ(2, reader.enqueued()[0].End());
}

}  // namespace
}  // namespace minfs
