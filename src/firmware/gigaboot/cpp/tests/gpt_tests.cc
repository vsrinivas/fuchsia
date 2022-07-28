// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <efi/types.h>
#include <gtest/gtest.h>

#include "gpt.h"
#include "mock_boot_service.h"
#include "utils.h"

namespace gigaboot {
namespace {

TEST(GigabootTest, FindEfiGptDevice) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
}

TEST(GigabootTest, FindEfiGptDeviceNoMatchingDevicePath) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-D"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  // The device path doesn't match. Should fail.
  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindEfiGptDeviceIgnoreLogicalPartition) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.block_io_media().LogicalPartition = true;

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindEfiGptDeviceIgnoreNotPresentMedia) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.block_io_media().MediaPresent = false;

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindPartition) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.InitializeGpt();
  gpt_entry_t zircon_a_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, zircon_a_entry);
  block_device.AddGptPartition(zircon_a_entry);
  gpt_entry_t zircon_b_entry{{}, {}, kGptFirstUsableBlocks + 10, kGptFirstUsableBlocks + 20, 0, {}};
  SetGptEntryName(GPT_ZIRCON_B_NAME, zircon_b_entry);
  block_device.AddGptPartition(zircon_b_entry);
  block_device.FinalizeGpt();

  // Try to find the zircon_a partition.
  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  const gpt_entry_t* find_res = res.value().FindPartition(GPT_ZIRCON_A_NAME);
  ASSERT_NE(find_res, nullptr);
  ASSERT_EQ(memcmp(find_res, &zircon_a_entry, sizeof(gpt_entry_t)), 0);

  find_res = res.value().FindPartition(GPT_ZIRCON_B_NAME);
  ASSERT_NE(find_res, nullptr);
  ASSERT_EQ(memcmp(find_res, &zircon_b_entry, sizeof(gpt_entry_t)), 0);

  // Non-existing partition returns nullptr.
  ASSERT_EQ(res.value().FindPartition(GPT_ZIRCON_R_NAME), nullptr);
}

TEST(GigabootTest, FindEfiGptDeviceNoGpt) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_error());
}

class GptReadWriteTest : public ::testing::Test {
 public:
  GptReadWriteTest()
      : image_device_({"path-A", "path-B", "path-C", "image"}),
        block_device_({"path-A", "path-B", "path-C"}, 1024) {
    stub_service_.AddDevice(&image_device_);
    stub_service_.AddDevice(&block_device_);
    block_device_.InitializeGpt();
  }

  void AddPartition(const gpt_entry_t& new_entry) {
    block_device_.AddGptPartition(new_entry);
    block_device_.FinalizeGpt();
  }

  uint8_t* BlockDeviceStart() { return block_device_.fake_disk_io_protocol().contents(0).data(); }

  MockStubService& stub_service() { return stub_service_; }
  Device& image_device() { return image_device_; }
  BlockDevice& block_device() { return block_device_; }

 private:
  MockStubService stub_service_;
  Device image_device_;
  BlockDevice block_device_;
};

TEST_F(GptReadWriteTest, ReadWritePartition) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  // Add a zircon_a entry for test.
  gpt_entry_t new_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, new_entry);
  AddPartition(new_entry);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  uint8_t* partition_start = BlockDeviceStart() + new_entry.first * kBlockSize;

  // Write something to the partition.
  const char write_content[] = "write content";
  auto write_res = res->WritePartition(GPT_ZIRCON_A_NAME, write_content, 0, sizeof(write_content));
  ASSERT_TRUE(write_res.is_ok());
  ASSERT_TRUE(memcmp(partition_start, write_content, sizeof(write_content)) == 0);

  // Read the partition.
  const char expected_read_content[] = "read content";
  // Modify the storage directly.
  memcpy(partition_start, expected_read_content, sizeof(expected_read_content));
  char read_content[sizeof(expected_read_content)];
  auto read_res = res->ReadPartition(GPT_ZIRCON_A_NAME, 0, sizeof(read_content), read_content);
  ASSERT_TRUE(read_res.is_ok());
  ASSERT_TRUE(memcmp(read_content, expected_read_content, sizeof(read_content)) == 0);
}

TEST_F(GptReadWriteTest, ReadWritePartitionWithOffset) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  // Add a zircon_a entry for test.
  gpt_entry_t new_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, new_entry);
  AddPartition(new_entry);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  uint8_t* partition_start = BlockDeviceStart() + new_entry.first * kBlockSize;

  constexpr size_t kOffset = 16;
  // Write something to the partition
  const char write_content[] = "write content";
  auto write_res =
      res->WritePartition(GPT_ZIRCON_A_NAME, write_content, kOffset, sizeof(write_content));
  ASSERT_TRUE(write_res.is_ok());
  ASSERT_TRUE(memcmp(partition_start + kOffset, write_content, sizeof(write_content)) == 0);

  // Read the partition
  const char expected_read_content[] = "read content";
  // Modify the storage directly.
  memcpy(partition_start + kOffset, expected_read_content, sizeof(expected_read_content));
  char read_content[sizeof(expected_read_content)];
  auto read_res =
      res->ReadPartition(GPT_ZIRCON_A_NAME, kOffset, sizeof(read_content), read_content);
  ASSERT_TRUE(read_res.is_ok());
  ASSERT_TRUE(memcmp(read_content, expected_read_content, sizeof(read_content)) == 0);
}

TEST_F(GptReadWriteTest, ReadWritePartitionOutOfBound) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  // Add a zircon_a entry for test.
  constexpr size_t kPartitionBlocks = 5;
  gpt_entry_t new_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + kPartitionBlocks - 1,
                        0,  {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, new_entry);
  AddPartition(new_entry);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  // Write something to the partition
  const char write_content[] = "write content";
  // Use a offset that'll cause out-of-range write.
  constexpr size_t kOffset = kPartitionBlocks * kBlockSize - sizeof(write_content) + 1;
  auto write_res =
      res->WritePartition(GPT_ZIRCON_A_NAME, write_content, kOffset, sizeof(write_content));
  ASSERT_TRUE(write_res.is_error());

  // Read the partition
  char read_content[sizeof(write_content)];
  auto read_res =
      res->ReadPartition(GPT_ZIRCON_A_NAME, kOffset, sizeof(read_content), read_content);
  ASSERT_TRUE(read_res.is_error());
}

TEST_F(GptReadWriteTest, ReadWritePartitionNonExistingPartition) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  // Add a zircon_a entry for test.
  gpt_entry_t new_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, new_entry);
  AddPartition(new_entry);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  // Write something to the partition.
  const char write_content[] = "write content";
  auto write_res = res->WritePartition(GPT_ZIRCON_B_NAME, write_content, 0, sizeof(write_content));
  ASSERT_TRUE(write_res.is_error());

  // Read the partition.
  char read_content[sizeof(write_content)];
  auto read_res = res->ReadPartition(GPT_ZIRCON_B_NAME, 0, sizeof(read_content), read_content);
  ASSERT_TRUE(read_res.is_error());
}

}  // namespace

}  // namespace gigaboot
