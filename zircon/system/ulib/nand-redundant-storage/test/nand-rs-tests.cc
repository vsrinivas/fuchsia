// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/mtd/mtd-interface.h>
#include <lib/nand-redundant-storage/file-nand-redundant-storage.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>
#include <stdint.h>

#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// This NAND interface test relies on an MTD device file located at /dev/mtd0/
// for non-astro tests, and /dev/mtd/mtd9 for astro-based tests.
//
// On the host machine, nandsim is used to create a virtual MTD device.
// The following command was used to create the device for this test.
//
// $ sudo modprobe nandsim id_bytes=0x2c,0xdc,0x90,0xa6,0x54,0x0 badblocks=5

using namespace nand_rs;

namespace {

const uint32_t kFilePageSize = 4096;
const uint32_t kFileBlockSize = kFilePageSize * 64;

#ifdef ASTRO
constexpr const char* kTestDevicePath = "/dev/mtd/mtd9";
#else
constexpr const char* kTestDevicePath = "/dev/mtd0";
#endif

class MtdRsTest : public zxtest::Test {
 protected:
  void SetUp() override {
    auto mtd = mtd::MtdInterface::Create(kTestDevicePath);
    mtd_ptr_ = mtd.get();
    // Sanity "test" to make sure the interface is valid.
    ASSERT_NE(nullptr, mtd_ptr_, "Failed to initialize nand_mtd_ with device %s", kTestDevicePath);
    nand_mtd_ = NandRedundantStorage::Create(std::move(mtd));

    num_copies_written_ = 0;
    Wipe();

    file_ptr_ = std::tmpfile();
    fbl::unique_fd fd(fileno(file_ptr_));
    ASSERT_TRUE(fd);
    file_mtd_ =
        std::make_unique<FileNandRedundantStorage>(std::move(fd), kFileBlockSize, kFilePageSize);
  }

  void Wipe() {
    if (!is_nand_) {
      return;
    }

    for (uint32_t offset = 0; offset < mtd_ptr_->Size(); offset += mtd_ptr_->BlockSize()) {
      bool is_bad_block;
      ASSERT_OK(mtd_ptr_->IsBadBlock(offset, &is_bad_block));
      if (is_bad_block) {
        continue;
      }
      ASSERT_OK(mtd_ptr_->EraseBlock(offset));
    }
  }

  uint32_t BlockSize() {
    if (is_nand_) {
      return mtd_ptr_->BlockSize();
    }

    return file_mtd_->BlockSize();
  }

  uint32_t PageSize() {
    if (is_nand_) {
      return mtd_ptr_->PageSize();
    }

    return file_mtd_->PageSize();
  }

  NandRedundantStorageInterface* NandInterface() {
    if (is_nand_) {
      return nand_mtd_.get();
    }
    return file_mtd_.get();
  }

  // Zero-index-based block erase.
  void EraseBlockAtIndex(uint32_t index) {
    if (is_nand_) {
      ASSERT_OK(mtd_ptr_->EraseBlock(BlockSize() * index));
      return;
    }

    auto block_size = BlockSize();
    std::vector<uint8_t> buffer(block_size, 0);
    fpos_t current;
    ASSERT_EQ(0, fgetpos(file_ptr_, &current));
    ASSERT_EQ(0, fseek(file_ptr_, block_size * index, SEEK_SET));
    ASSERT_EQ(block_size, fwrite(buffer.data(), sizeof(uint8_t), buffer.size(), file_ptr_));
    ASSERT_EQ(0, fsetpos(file_ptr_, &current));
  }

  void WritePage(uint32_t offset, const std::vector<uint8_t>& buffer) {
    if (is_nand_) {
      ASSERT_OK(mtd_ptr_->WritePage(offset, buffer.data(), nullptr));
      return;
    }

    auto page_size = PageSize();
    fpos_t current;
    ASSERT_EQ(0, fgetpos(file_ptr_, &current));
    ASSERT_EQ(0, fseek(file_ptr_, offset, SEEK_SET));
    ASSERT_EQ(page_size, fwrite(buffer.data(), sizeof(uint8_t), buffer.size(), file_ptr_));
    ASSERT_EQ(0, fsetpos(file_ptr_, &current));
  }

  std::vector<uint8_t> MakeFakePage(uint8_t value, uint32_t checksum, uint32_t file_size) {
    std::vector<uint8_t> res(PageSize(), value);
    res[0] = 'Z';
    res[1] = 'N';
    res[2] = 'N';
    res[3] = 'D';

    // Avoid byte-masking, as the struct is just copied into memory.
    auto checksum_ptr = reinterpret_cast<uint32_t*>(&res[4]);
    *checksum_ptr = checksum;
    auto file_size_ptr = reinterpret_cast<uint32_t*>(&res[8]);
    *file_size_ptr = file_size;
    return res;
  }

  // Forward Declare Test Functions
  void ReadWriteTest();
  void WriteNoHeaderTest();
  void WriteNoHeaderWithoutFileSizeTest();
  void ReadWriteWithErasedBlockTest();
  void ReadWriteWithCorruptedBlockValidHeaderTest();
  void ReadWiteWithCorruptedBlockWrongCrcTest();
  void ReadWriteWithCorruptedBlockWrongHeaderTest();
  void ReadEmptyMtdTest();
  void BlockWriteLimitsTest();

  bool is_nand_ = true;

 private:
  mtd::MtdInterface* mtd_ptr_;
  FILE* file_ptr_;
  std::unique_ptr<NandRedundantStorage> nand_mtd_;
  std::unique_ptr<FileNandRedundantStorage> file_mtd_;
  std::vector<uint8_t> out_buffer_;
  uint32_t num_copies_written_;
};

// Test function Implementations

void MtdRsTest::ReadWriteTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> nonsense_buffer = {12, 14, 22, 0, 12, 8, 0, 0, 0, 3, 45, 0xFF};

  ASSERT_OK(mtd->WriteBuffer(nonsense_buffer, 10, &num_copies_written_));
  ASSERT_EQ(10, num_copies_written_);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), nonsense_buffer.data(), nonsense_buffer.size());

  std::vector<uint8_t> page_crossing_buffer(PageSize() * 2 + 13, 0xF5);
  ASSERT_OK(mtd->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
  ASSERT_EQ(10, num_copies_written_);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), page_crossing_buffer.data(), page_crossing_buffer.size());
}

void MtdRsTest::WriteNoHeaderTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> nonsense_buffer = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  size_t buffer_size = nonsense_buffer.size();

  ASSERT_OK(mtd->WriteBuffer(nonsense_buffer, 10, &num_copies_written_, true));
  ASSERT_EQ(10, num_copies_written_);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_, true, buffer_size));

  ASSERT_BYTES_EQ(out_buffer_.data(), nonsense_buffer.data(), nonsense_buffer.size());
}

void MtdRsTest::WriteNoHeaderWithoutFileSizeTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> nonsense_buffer = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  ASSERT_OK(mtd->WriteBuffer(nonsense_buffer, 10, &num_copies_written_, true));
  ASSERT_EQ(10, num_copies_written_);

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, mtd->ReadToBuffer(&out_buffer_, true));
}

void MtdRsTest::ReadWriteWithErasedBlockTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> page_crossing_buffer(PageSize() * 2 + 13, 0xF5);
  ASSERT_OK(mtd->WriteBuffer(page_crossing_buffer, 20, &num_copies_written_));
  ASSERT_EQ(20, num_copies_written_);

  EraseBlockAtIndex(0);
  EraseBlockAtIndex(1);
  EraseBlockAtIndex(2);
  EraseBlockAtIndex(3);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), page_crossing_buffer.data(), page_crossing_buffer.size());
}

void MtdRsTest::ReadWriteWithCorruptedBlockValidHeaderTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> page_crossing_buffer(PageSize() * 2 + 13, 0xF5);
  ASSERT_OK(mtd->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
  ASSERT_EQ(10, num_copies_written_);

  EraseBlockAtIndex(0);
  EraseBlockAtIndex(1);
  EraseBlockAtIndex(2);
  EraseBlockAtIndex(3);
  uint32_t block_three_start = BlockSize() * 2;
  std::vector<uint8_t> page_of_nonsense = MakeFakePage(0x40, 0x40404040, 0x40404040);
  WritePage(block_three_start, page_of_nonsense);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), page_crossing_buffer.data(), page_crossing_buffer.size());
}

void MtdRsTest::ReadWiteWithCorruptedBlockWrongCrcTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> page_crossing_buffer(PageSize() * 2 + 13, 0xF5);
  ASSERT_OK(mtd->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
  ASSERT_EQ(10, num_copies_written_);

  EraseBlockAtIndex(0);
  EraseBlockAtIndex(1);
  EraseBlockAtIndex(2);
  EraseBlockAtIndex(3);
  // Nonsense block, but with valid looking CRC and file size.
  uint32_t block_three_start = BlockSize() * 2;
  std::vector<uint8_t> page_of_nonsense = MakeFakePage(0x40, 1, 34);
  WritePage(block_three_start, page_of_nonsense);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), page_crossing_buffer.data(), page_crossing_buffer.size());
}

void MtdRsTest::ReadWriteWithCorruptedBlockWrongHeaderTest() {
  auto mtd = NandInterface();

  std::vector<uint8_t> page_crossing_buffer(PageSize() * 2 + 13, 0xF5);
  ASSERT_OK(mtd->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
  ASSERT_EQ(10, num_copies_written_);

  EraseBlockAtIndex(0);
  EraseBlockAtIndex(1);
  EraseBlockAtIndex(2);
  EraseBlockAtIndex(3);
  // Nonsense block, but with invalid header.
  uint32_t block_three_start = BlockSize() * 2;
  std::vector<uint8_t> page_of_nonsense = MakeFakePage(0x40, 1, 34);
  page_of_nonsense[0] = 'z';
  WritePage(block_three_start, page_of_nonsense);
  ASSERT_OK(mtd->ReadToBuffer(&out_buffer_));
  ASSERT_BYTES_EQ(out_buffer_.data(), page_crossing_buffer.data(), page_crossing_buffer.size());
}

void MtdRsTest::ReadEmptyMtdTest() {
  ASSERT_EQ(ZX_ERR_IO, NandInterface()->ReadToBuffer(&out_buffer_));
}

void MtdRsTest::BlockWriteLimitsTest() {
  auto mtd = NandInterface();

  uint32_t max_blocks = mtd_ptr_->Size() / mtd_ptr_->BlockSize();
  std::vector<uint8_t> some_bits = {1, 2, 3, 5, 10, 9, 25, 83};
  ASSERT_OK(mtd->WriteBuffer(some_bits, max_blocks, &num_copies_written_));
  ASSERT_EQ(max_blocks - 1, num_copies_written_);
}

// Register All Tests
// TODO(fxbug.dev/44390): Convert to parameterized tests instead of custom macro

TEST_F(MtdRsTest, NandReadWriteTest) {
  is_nand_ = true;
  ReadWriteTest();
}

TEST_F(MtdRsTest, FileReadWriteTest) {
  is_nand_ = false;
  ReadWriteTest();
}

TEST_F(MtdRsTest, NandWriteNoHeaderTest) {
  is_nand_ = true;
  WriteNoHeaderTest();
}

TEST_F(MtdRsTest, FileWriteNoHeaderTest) {
  is_nand_ = false;
  WriteNoHeaderTest();
}

TEST_F(MtdRsTest, NandWriteNoHeaderWithoutFileSizeTest) {
  is_nand_ = true;
  WriteNoHeaderWithoutFileSizeTest();
}

TEST_F(MtdRsTest, FileWriteNoHeaderWithoutFileSizeTest) {
  is_nand_ = false;
  WriteNoHeaderWithoutFileSizeTest();
}

TEST_F(MtdRsTest, NandReadWriteWithErasedBlockTest) {
  is_nand_ = true;
  ReadWriteWithErasedBlockTest();
}

TEST_F(MtdRsTest, FileReadWriteWithErasedBlockTest) {
  is_nand_ = false;
  ReadWriteWithErasedBlockTest();
}

TEST_F(MtdRsTest, NandReadWriteWithCorruptedBlockValidHeaderTest) {
  is_nand_ = true;
  ReadWriteWithCorruptedBlockValidHeaderTest();
}

TEST_F(MtdRsTest, FileReadWriteWithCorruptedBlockValidHeaderTest) {
  is_nand_ = false;
  ReadWriteWithCorruptedBlockValidHeaderTest();
}

TEST_F(MtdRsTest, NandReadWiteWithCorruptedBlockWrongCrcTest) {
  is_nand_ = true;
  ReadWiteWithCorruptedBlockWrongCrcTest();
}

TEST_F(MtdRsTest, FileReadWiteWithCorruptedBlockWrongCrcTest) {
  is_nand_ = false;
  ReadWiteWithCorruptedBlockWrongCrcTest();
}

TEST_F(MtdRsTest, NandReadWriteWithCorruptedBlockWrongHeaderTest) {
  is_nand_ = true;
  ReadWriteWithCorruptedBlockWrongHeaderTest();
}

TEST_F(MtdRsTest, FileReadWriteWithCorruptedBlockWrongHeaderTest) {
  is_nand_ = false;
  ReadWriteWithCorruptedBlockWrongHeaderTest();
}

TEST_F(MtdRsTest, NandReadEmptyMtdTest) {
  is_nand_ = true;
  ReadEmptyMtdTest();
}

TEST_F(MtdRsTest, FileReadEmptyMtdTest) {
  is_nand_ = false;
  ReadEmptyMtdTest();
}

// Only Nand backed devices have block limits
TEST_F(MtdRsTest, NandBlockWriteLimitsTest) {
  is_nand_ = true;
  BlockWriteLimitsTest();
}

}  // namespace
