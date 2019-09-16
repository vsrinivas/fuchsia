// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/blobfs.h>

#include <blobfs/format.h>
#include <block-client/cpp/fake-device.h>
#include <fs/buffer/vmo_buffer.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

using block_client::BlockDevice;
using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

std::unique_ptr<FakeBlockDevice> CreateAndFormatDevice() {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  EXPECT_OK(FormatFilesystem(device.get()));
  if (CURRENT_TEST_HAS_FAILURES()) {
    return nullptr;
  }
  return device;
}

class BlobfsTest : public zxtest::Test {
 public:
  void SetUp() final {
    MountOptions options;
    std::unique_ptr<FakeBlockDevice> device = CreateAndFormatDevice();
    ASSERT_TRUE(device);
    device_ = device.get();
    ASSERT_OK(Blobfs::Create(std::move(device), &options, &fs_));
  }

 protected:
  FakeBlockDevice* device_ = nullptr;
  std::unique_ptr<Blobfs> fs_;
};

TEST_F(BlobfsTest, GetDevice) {
  ASSERT_EQ(device_, fs_->GetDevice());
}

TEST_F(BlobfsTest, BlockNumberToDevice) {
  ASSERT_EQ(42 * kBlobfsBlockSize / kBlockSize, fs_->BlockNumberToDevice(42));
}

// Tests reading a well known location.
TEST_F(BlobfsTest, RunOperationExpectedRead) {
  fs::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"));

  // Read the first block.
  fs::Operation operation = {};
  operation.type = fs::OperationType::kRead;
  operation.length = 1;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  uint64_t* data = reinterpret_cast<uint64_t*>(buffer.Data(0));
  EXPECT_EQ(kBlobfsMagic0, data[0]);
  EXPECT_EQ(kBlobfsMagic1, data[1]);
}

// Tests that we can read back what we write.
TEST_F(BlobfsTest, RunOperationReadWrite) {
  char data[kBlobfsBlockSize] = "something to test";

  fs::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"));
  memcpy(buffer.Data(0), data, kBlobfsBlockSize);

  fs::Operation operation = {};
  operation.type = fs::OperationType::kWrite;
  operation.dev_offset = 1;
  operation.length = 1;

  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  memset(buffer.Data(0), 'a', kBlobfsBlockSize);
  operation.type = fs::OperationType::kRead;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  ASSERT_BYTES_EQ(data, buffer.Data(0), kBlobfsBlockSize);
}

}  // namespace
}  // namespace blobfs
