// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition-client.h"

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <string.h>
#include <zircon/hw/gpt.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "test-utils.h"
#include "zircon/errors.h"

namespace {

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

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

class SherlockBootloaderPartitionClientTest : public zxtest::Test {
 public:
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));

    constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, 2, 512, &gpt_dev_));
    ASSERT_OK(fdio_get_service_handle(gpt_dev_->fd(), service_channel_.reset_and_get_address()));
  }

  // Creates a BlockPartitionClient which will read/write the entire device.
  std::unique_ptr<paver::BlockPartitionClient> RawClient() {
    return std::make_unique<paver::BlockPartitionClient>(
        zx::channel(fdio_service_clone(service_channel_.get())));
  }

  // Creates a SherlockBootloaderPartitionClient which will read/write using
  // the custom sherlock bootloader logic.
  std::unique_ptr<paver::SherlockBootloaderPartitionClient> BootloaderClient() {
    return std::make_unique<paver::SherlockBootloaderPartitionClient>(
        zx::channel(fdio_service_clone(service_channel_.get())));
  }

 private:
  IsolatedDevmgr devmgr_;
  std::unique_ptr<BlockDevice> gpt_dev_;
  zx::channel service_channel_;
};

// Writes |data| to |client|.
// Call with ASSERT_NO_FATAL_FAILURES().
void Write(std::unique_ptr<paver::PartitionClient> client, std::string_view data) {
  // Write data to a VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(data.size(), 0, &vmo));
  ASSERT_OK(vmo.write(data.data(), 0, data.size()));

  // Write VMO to the client.
  ASSERT_OK(client->Write(vmo, data.size()));
}

// Reads |size| bytes from |client| into |data|.
// Call with ASSERT_NO_FATAL_FAILURES().
void Read(std::unique_ptr<paver::PartitionClient> client, std::string* data, size_t size) {
  data->resize(size);

  // Read client to a VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(data->size(), 0, &vmo));
  ASSERT_OK(client->Read(vmo, data->size()));

  // Read VMO to data.
  ASSERT_OK(vmo.read(data->data(), 0, data->size()));
}

TEST_F(SherlockBootloaderPartitionClientTest, DISABLED_BootloaderPartitionSize) {
  size_t size = 0;

  ASSERT_OK(RawClient()->GetPartitionSize(&size));
  ASSERT_EQ(1024, size);

  // Bootloader size should not count block 0.
  ASSERT_OK(BootloaderClient()->GetPartitionSize(&size));
  ASSERT_EQ(512, size);
}

TEST_F(SherlockBootloaderPartitionClientTest, DISABLED_ReadBootloaderPartition) {
  const std::string block0(512, '0');
  const std::string firmware(512, 'F');

  ASSERT_NO_FATAL_FAILURES(Write(RawClient(), block0 + firmware));

  // Bootloader read should skip block 0.
  std::string actual;
  ASSERT_NO_FATAL_FAILURES(Read(BootloaderClient(), &actual, 512));
  ASSERT_EQ(firmware, actual);
}

TEST_F(SherlockBootloaderPartitionClientTest, DISABLED_WriteBootloaderPartition) {
  const std::string block0(512, '0');
  const std::string firmware(512, 'F');

  ASSERT_NO_FATAL_FAILURES(Write(RawClient(), block0 + block0));
  ASSERT_NO_FATAL_FAILURES(Write(BootloaderClient(), firmware));

  // Bootloader write should have skipped block 0.
  std::string actual;
  ASSERT_NO_FATAL_FAILURES(Read(RawClient(), &actual, 1024));
  ASSERT_EQ(block0 + firmware, actual);
}

}  // namespace
