// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <vector>

#include <lib/mtd/mtd-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>
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

#ifdef ASTRO
constexpr const char* kTestDevicePath = "/dev/mtd/mtd9";
#else
constexpr const char* kTestDevicePath = "/dev/mtd0";
#endif

class MtdRsTest : public zxtest::Test {
protected:
    void Wipe() {
        for (uint32_t offset = 0; offset < mtd_ptr_->Size(); offset += mtd_ptr_->BlockSize()) {
            bool is_bad_block;
            ASSERT_EQ(ZX_OK, mtd_ptr_->IsBadBlock(offset, &is_bad_block));
            if (is_bad_block) {
                continue;
            }
            ASSERT_EQ(ZX_OK, mtd_ptr_->EraseBlock(offset));
        }
    }

    // Zero-index-based block erase.
    void EraseBlockAtIndex(uint32_t index) {
        ASSERT_EQ(ZX_OK, mtd_ptr_->EraseBlock(mtd_ptr_->BlockSize() * index));
    }

    void SetUp() override {
        auto mtd = mtd::MtdInterface::Create(kTestDevicePath);
        mtd_ptr_ = mtd.get();
        // Sanity "test" to make sure the interface is valid.
        ASSERT_NE(nullptr, mtd_ptr_, "Failed to initialize nand_ with device %s", kTestDevicePath);
        nand_ = NandRedundantStorage::Create(std::move(mtd));

        max_blocks_ = mtd_ptr_->Size() / mtd_ptr_->BlockSize();
        num_copies_written_ = 0;
        Wipe();
    }

    std::vector<uint8_t> MakeFakePage(uint8_t value, uint32_t checksum, uint32_t file_size) {
        std::vector<uint8_t> res(mtd_ptr_->PageSize(), value);
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

    mtd::MtdInterface* mtd_ptr_;
    std::unique_ptr<NandRedundantStorage> nand_;
    std::vector<uint8_t> out_buffer_;
    uint32_t num_copies_written_;
    uint32_t max_blocks_;
};

TEST_F(MtdRsTest, ReadWriteTest) {
    std::vector<uint8_t> nonsense_buffer = {12, 14, 22, 0, 12, 8, 0, 0, 0, 3, 45, 0xFF};

    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(nonsense_buffer, 10, &num_copies_written_));
    ASSERT_EQ(10, num_copies_written_);
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, nonsense_buffer);

    std::vector<uint8_t> page_crossing_buffer(mtd_ptr_->PageSize() * 2 + 13, 0xF5);
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
    ASSERT_EQ(10, num_copies_written_);
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, page_crossing_buffer);
}

TEST_F(MtdRsTest, ReadWriteTestWithErasedBlock) {
    std::vector<uint8_t> page_crossing_buffer(mtd_ptr_->PageSize() * 2 + 13, 0xF5);
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(page_crossing_buffer, 20, &num_copies_written_));
    ASSERT_EQ(20, num_copies_written_);

    EraseBlockAtIndex(0);
    EraseBlockAtIndex(1);
    EraseBlockAtIndex(2);
    EraseBlockAtIndex(3);
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, page_crossing_buffer);
}

TEST_F(MtdRsTest, ReadWriteTestWithCorruptedBlockValidHeader) {
    std::vector<uint8_t> page_crossing_buffer(mtd_ptr_->PageSize() * 2 + 13, 0xF5);
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
    ASSERT_EQ(10, num_copies_written_);

    EraseBlockAtIndex(0);
    EraseBlockAtIndex(1);
    EraseBlockAtIndex(2);
    EraseBlockAtIndex(3);
    uint32_t block_three_start = mtd_ptr_->BlockSize() * 2;
    std::vector<uint8_t> page_of_nonsense =
        MakeFakePage(0x40, 0x40404040, 0x40404040);
    ASSERT_EQ(ZX_OK, mtd_ptr_->WritePage(
                         block_three_start, page_of_nonsense.data(), nullptr));
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, page_crossing_buffer);
}

TEST_F(MtdRsTest, ReadWiteTestWithCorruptedBlockWrongCrc) {
    std::vector<uint8_t> page_crossing_buffer(mtd_ptr_->PageSize() * 2 + 13, 0xF5);
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
    ASSERT_EQ(10, num_copies_written_);

    EraseBlockAtIndex(0);
    EraseBlockAtIndex(1);
    EraseBlockAtIndex(2);
    EraseBlockAtIndex(3);
    // Nonsense block, but with valid looking CRC and file size.
    uint32_t block_three_start = mtd_ptr_->BlockSize() * 2;
    std::vector<uint8_t> page_of_nonsense = MakeFakePage(0x40, 1, 34);
    ASSERT_EQ(ZX_OK, mtd_ptr_->WritePage(
                         block_three_start, page_of_nonsense.data(), nullptr));
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, page_crossing_buffer);
}

TEST_F(MtdRsTest, ReadWriteTestWithCorruptedBlockWrongHeader) {
    std::vector<uint8_t> page_crossing_buffer(mtd_ptr_->PageSize() * 2 + 13, 0xF5);
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(page_crossing_buffer, 10, &num_copies_written_));
    ASSERT_EQ(10, num_copies_written_);

    EraseBlockAtIndex(0);
    EraseBlockAtIndex(1);
    EraseBlockAtIndex(2);
    EraseBlockAtIndex(3);
    // Nonsense block, but with invalid header.
    uint32_t block_three_start = mtd_ptr_->BlockSize() * 2;
    std::vector<uint8_t> page_of_nonsense = MakeFakePage(0x40, 1, 34);
    page_of_nonsense[0] = 'z';
    ASSERT_EQ(ZX_OK, mtd_ptr_->WritePage(
                         block_three_start, page_of_nonsense.data(), nullptr));
    ASSERT_EQ(ZX_OK, nand_->ReadToBuffer(&out_buffer_));
    ASSERT_EQ(out_buffer_, page_crossing_buffer);
}

TEST_F(MtdRsTest, ReadEmptyMtd) {
    ASSERT_EQ(ZX_ERR_IO, nand_->ReadToBuffer(&out_buffer_));
}

TEST_F(MtdRsTest, TestBlockWriteLimits) {
    std::vector<uint8_t> some_bits = {1, 2, 3, 5, 10, 9, 25, 83};
    ASSERT_EQ(ZX_OK, nand_->WriteBuffer(some_bits, max_blocks_, &num_copies_written_));
    ASSERT_EQ(max_blocks_ - 1, num_copies_written_);
}

} // namespace
