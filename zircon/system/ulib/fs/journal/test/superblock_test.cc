// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/journal/superblock.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

const uint32_t kBlockSize = 8192;

class Buffer : public storage::BlockBuffer {
 public:
  Buffer() : buffer_(std::make_unique<uint8_t[]>(kBlockSize)) {}

  size_t capacity() const final { return 1; }
  uint32_t BlockSize() const final { return kBlockSize; }
  vmoid_t vmoid() const final { return BLOCK_VMOID_INVALID; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final { return &buffer_[index * kBlockSize]; }
  const void* Data(size_t index) const final { return &buffer_[index * kBlockSize]; }

 private:
  std::unique_ptr<uint8_t[]> buffer_;
};

class JournalSuperblockFixture : public testing::Test {
 public:
  void SetUp() override {
    // Grab a backdoor to the Buffer object, so we can change it while the superblock has
    // ownership.
    buffer_ = std::make_unique<Buffer>();
    buffer_ptr_ = static_cast<uint8_t*>(buffer_->Data(0));
  }

  std::unique_ptr<storage::BlockBuffer> take_buffer() { return std::move(buffer_); }

  JournalInfo* info() { return reinterpret_cast<JournalInfo*>(&buffer_ptr_[0]); }

 private:
  std::unique_ptr<Buffer> buffer_;
  uint8_t* buffer_ptr_;
};

using JournalSuperblockTest = JournalSuperblockFixture;

TEST_F(JournalSuperblockTest, UpdateChangesStartAndSequenceNumber) {
  JournalSuperblock superblock(take_buffer());
  uint64_t kStart = 1234;
  uint64_t kSequenceNumber = 5678;
  superblock.Update(kStart, kSequenceNumber);
  EXPECT_EQ(kStart, superblock.start());
  EXPECT_EQ(kSequenceNumber, superblock.sequence_number());
  EXPECT_EQ(superblock.Validate(), ZX_OK);
}

TEST_F(JournalSuperblockTest, EmptySuperblockIsNotValid) {
  JournalSuperblock superblock(take_buffer());
  EXPECT_EQ(ZX_ERR_IO, superblock.Validate()) << "An unset superblock should be invalid";
}

TEST_F(JournalSuperblockTest, BadChecksumDoesNotValidate) {
  JournalSuperblock superblock(take_buffer());
  superblock.Update(1234, 5678);
  EXPECT_EQ(superblock.Validate(), ZX_OK) << "Superblock should be valid after Update";

  // Let's pretend a bit was flipped while on disk.
  info()->timestamp++;

  EXPECT_EQ(superblock.Validate(), ZX_ERR_IO) << "Superblock shouldn't be valid with bad sequence";
}

}  // namespace
}  // namespace fs
