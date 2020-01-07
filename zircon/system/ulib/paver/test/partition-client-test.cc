// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition-client.h"

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "zircon/errors.h"

class FakePartitionClient final : public paver::PartitionClient {
 public:
  explicit FakePartitionClient(size_t block_size, size_t partition_size)
      : block_size_(block_size), partition_size_(partition_size) {}

  zx_status_t GetBlockSize(size_t* out_size) final {
    *out_size = block_size_;
    return result_;
  }
  zx_status_t GetPartitionSize(size_t* out_size) final {
    *out_size = partition_size_;
    return result_;
  }
  zx_status_t Read(const zx::vmo& vmo, size_t size) final {
    read_called_ = true;
    if (size > partition_size_) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    return result_;
  }
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final {
    write_called_ = true;
    if (vmo_size > partition_size_) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    return result_;
  }
  zx_status_t Trim() final {
    trim_called_ = true;
    return result_;
  }
  zx_status_t Flush() final {
    flush_called_ = true;
    return result_;
  }

  zx::channel GetChannel() final { return {}; }
  fbl::unique_fd block_fd() final { return {}; }

  bool read_called() { return read_called_; }
  bool write_called() { return write_called_; }
  bool trim_called() { return trim_called_; }
  bool flush_called() { return flush_called_; }

  void set_result(zx_status_t result) { result_ = result; }

 private:
  size_t block_size_;
  size_t partition_size_;

  bool read_called_ = false;
  bool write_called_ = false;
  bool trim_called_ = false;
  bool flush_called_ = false;
  zx_status_t result_ = ZX_OK;
};

TEST(PartitionCopyClientTest, ConstructEmpty) { paver::PartitionCopyClient({}); }

TEST(PartitionCopyClientTest, ConstructSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));
}

TEST(PartitionCopyClientTest, GetBlockSizeSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  size_t block_size;
  ASSERT_OK(client.GetBlockSize(&block_size));
  ASSERT_EQ(block_size, 10);

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetBlockSize(&block_size), ZX_OK);
}

TEST(PartitionCopyClientTest, GetPartitionSizeSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  size_t partition_size;
  ASSERT_OK(client.GetPartitionSize(&partition_size));
  ASSERT_EQ(partition_size, 100);

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetPartitionSize(&partition_size), ZX_OK);
}

TEST(PartitionCopyClientTest, ReadSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Read(vmo, 0));
  ASSERT_TRUE(fake_ref->read_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Read(vmo, 0), ZX_OK);
}

TEST(PartitionCopyClientTest, WriteSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Write(vmo, 0));
  ASSERT_TRUE(fake_ref->write_called());
  ASSERT_FALSE(fake_ref->trim_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Write(vmo, 0), ZX_OK);
  ASSERT_TRUE(fake_ref->trim_called());
}

TEST(PartitionCopyClientTest, TrimSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Trim());
  ASSERT_TRUE(fake_ref->trim_called());

  fake_ref->set_result(ZX_ERR_NOT_SUPPORTED);
  ASSERT_NE(client.Trim(), ZX_OK);
}

TEST(PartitionCopyClientTest, FlushSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake_ref = fake.get();
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Flush());
  ASSERT_TRUE(fake_ref->flush_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Flush(), ZX_OK);
}

TEST(PartitionCopyClientTest, GetChannelSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  ASSERT_EQ(client.GetChannel(), zx::channel());
}

TEST(PartitionCopyClientTest, BlockFdSinglePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  partitions.push_back(std::move(fake));
  paver::PartitionCopyClient client(std::move(partitions));

  ASSERT_EQ(client.block_fd(), fbl::unique_fd());
}

TEST(PartitionCopyClientTest, ConstructMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));
}

TEST(PartitionCopyClientTest, GetBlockSizeMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  size_t block_size;
  ASSERT_OK(client.GetBlockSize(&block_size));
  ASSERT_EQ(block_size, 70);

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.GetBlockSize(&block_size));
  ASSERT_EQ(block_size, 7);

  fake_ref2->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetBlockSize(&block_size), ZX_OK);
}

TEST(PartitionCopyClientTest, GetPartitionSizeMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  size_t partition_size;
  ASSERT_OK(client.GetPartitionSize(&partition_size));
  ASSERT_EQ(partition_size, 90);

  fake_ref2->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.GetPartitionSize(&partition_size));
  ASSERT_EQ(partition_size, 100);

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetPartitionSize(&partition_size), ZX_OK);
}

TEST(PartitionCopyClientTest, ReadMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Read(vmo, 0));
  ASSERT_TRUE(fake_ref->read_called());
  ASSERT_FALSE(fake_ref2->read_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.Read(vmo, 0));
  ASSERT_TRUE(fake_ref2->read_called());

  fake_ref2->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Read(vmo, 0), ZX_OK);
}

TEST(PartitionCopyClientTest, WriteMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Write(vmo, 0));
  ASSERT_TRUE(fake_ref->write_called());
  ASSERT_TRUE(fake_ref2->write_called());
  ASSERT_FALSE(fake_ref->trim_called());
  ASSERT_FALSE(fake_ref->trim_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.Write(vmo, 0));
  ASSERT_TRUE(fake_ref->trim_called());
  ASSERT_FALSE(fake_ref2->trim_called());

  fake_ref2->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Write(vmo, 0), ZX_OK);
}

TEST(PartitionCopyClientTest, TrimMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Trim());
  ASSERT_TRUE(fake_ref->trim_called());
  ASSERT_TRUE(fake_ref2->trim_called());

  fake_ref->set_result(ZX_ERR_NOT_SUPPORTED);
  ASSERT_NE(client.Trim(), ZX_OK);
}

TEST(PartitionCopyClientTest, FlushMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  auto fake_ref = fake.get();
  auto fake_ref2 = fake2.get();
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  zx::vmo vmo;
  ASSERT_OK(client.Flush());
  ASSERT_TRUE(fake_ref->flush_called());
  ASSERT_TRUE(fake_ref2->flush_called());

  fake_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Flush(), ZX_OK);
}

TEST(PartitionCopyClientTest, GetChannelMultiplePartitions) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  ASSERT_EQ(client.GetChannel(), zx::channel());
}

TEST(PartitionCopyClientTest, BlockFdMultilplePartition) {
  std::vector<std::unique_ptr<paver::PartitionClient>> partitions;
  auto fake = std::make_unique<FakePartitionClient>(10, 100);
  auto fake2 = std::make_unique<FakePartitionClient>(7, 90);
  partitions.push_back(std::move(fake));
  partitions.push_back(std::move(fake2));
  paver::PartitionCopyClient client(std::move(partitions));

  ASSERT_EQ(client.block_fd(), fbl::unique_fd());
}

TEST(AstroBootloaderPartitionClientTest, ConstructEmpty) {
  paver::AstroBootloaderPartitionClient(nullptr, nullptr);
}

TEST(AstroBootloaderPartitionClientTest, ConstructValidPartitions) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));
}

TEST(AstroBootloaderPartitionClientTest, GetBlockSize) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  size_t block_size;
  ASSERT_OK(client.GetBlockSize(&block_size));
  ASSERT_EQ(block_size, 10);

  bl2_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetBlockSize(&block_size), ZX_OK);

  bl2_ref->set_result(ZX_OK);
  tpl_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.GetBlockSize(&block_size));
  ASSERT_EQ(block_size, 10);
}

TEST(AstroBootloaderPartitionClientTest, GetPartitionSize) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  size_t partition_size;
  ASSERT_OK(client.GetPartitionSize(&partition_size));
  ASSERT_EQ(partition_size, 200);

  bl2_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetPartitionSize(&partition_size), ZX_OK);

  bl2_ref->set_result(ZX_OK);
  tpl_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.GetPartitionSize(&partition_size), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, Read) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

  ASSERT_OK(client.Read(vmo, 100));
  ASSERT_TRUE(bl2_ref->read_called());
  ASSERT_FALSE(tpl_ref->read_called());

  ASSERT_OK(client.Read(vmo, 101));
  ASSERT_TRUE(tpl_ref->read_called());

  ASSERT_OK(client.Read(vmo, 200));
  ASSERT_NE(client.Read(vmo, 201), ZX_OK);

  tpl_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Read(vmo, 101), ZX_OK);
  ASSERT_OK(client.Read(vmo, 100));

  bl2_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Read(vmo, 100), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, WriteCombined) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(200, 0, &vmo));
  uint32_t tag = 0x4C4D4140;
  ASSERT_OK(vmo.write(&tag, 16, sizeof(tag)));

  ASSERT_OK(client.Write(vmo, 100));
  ASSERT_TRUE(bl2_ref->write_called());
  ASSERT_FALSE(tpl_ref->write_called());

  ASSERT_OK(client.Write(vmo, 120));
  ASSERT_TRUE(tpl_ref->write_called());

  ASSERT_OK(client.Write(vmo, 200));
  ASSERT_NE(client.Write(vmo, 201), ZX_OK);

  tpl_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Write(vmo, 120), ZX_OK);
  ASSERT_OK(client.Write(vmo, 100));

  bl2_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Write(vmo, 100), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, WriteTpl) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(200, 0, &vmo));
  uint32_t tag = 0xAA640001;
  ASSERT_OK(vmo.write(&tag, 16, sizeof(tag)));

  ASSERT_OK(client.Write(vmo, 100));
  ASSERT_FALSE(bl2_ref->write_called());
  ASSERT_TRUE(tpl_ref->write_called());

  ASSERT_NE(client.Write(vmo, 101), ZX_OK);

  bl2_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_OK(client.Write(vmo, 100));

  tpl_ref->set_result(ZX_ERR_ACCESS_DENIED);
  ASSERT_NE(client.Write(vmo, 100), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, Trim) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  ASSERT_OK(client.Trim());
  ASSERT_TRUE(bl2_ref->trim_called());
  ASSERT_TRUE(tpl_ref->trim_called());

  bl2_ref->set_result(ZX_ERR_NOT_SUPPORTED);
  ASSERT_NE(client.Trim(), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, Flush) {
  auto bl2 = std::make_unique<FakePartitionClient>(10, 100);
  auto tpl = std::make_unique<FakePartitionClient>(10, 100);
  auto bl2_ref = bl2.get();
  auto tpl_ref = tpl.get();
  paver::AstroBootloaderPartitionClient client(std::move(bl2), std::move(tpl));

  ASSERT_OK(client.Flush());
  ASSERT_TRUE(bl2_ref->flush_called());
  ASSERT_TRUE(tpl_ref->flush_called());

  bl2_ref->set_result(ZX_ERR_NOT_SUPPORTED);
  ASSERT_NE(client.Flush(), ZX_OK);
}

TEST(AstroBootloaderPartitionClientTest, GetChannel) {
  paver::AstroBootloaderPartitionClient client(nullptr, nullptr);
  ASSERT_EQ(client.GetChannel(), zx::channel());
}

TEST(AstroBootloaderPartitionClientTest, BlockFd) {
  paver::AstroBootloaderPartitionClient client(nullptr, nullptr);
  ASSERT_EQ(client.block_fd(), fbl::unique_fd());
}
