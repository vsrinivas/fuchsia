// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <memory>

#include <lib/ftl-mtd/nand-volume-driver.h>
#include <zxtest/zxtest.h>

#include "fake-nand-interface.h"

using namespace ftl_mtd;

namespace {

constexpr uint32_t kOobSizeDefault = 128;        // Produces page_multiplier of 1.
constexpr uint32_t kOobSizeNeedsMultiplier2 = 8; // Produces page_multiplier of 2.
constexpr uint32_t kPageSize = 4 * 1024;         // 4 KiB
constexpr uint32_t kBlockSize = 256 * 1024;      // 256 KiB
constexpr uint32_t kSize = 64 * 1024 * 1024;     // 64 MiB
constexpr uint32_t kMaxBadBlocks = 10;

class NandVolumeDriverTest : public zxtest::Test {
protected:
    void SetUpDriver(uint32_t block_offset, uint32_t group_size, uint32_t oob_size) {
        page_multiplier_ = std::max(1u, kMinimumOobSize / oob_size);
        oob_size_ = oob_size;
        group_size_ = group_size;

        read_page_buffer_ = std::make_unique<uint8_t[]>(
            group_size_ * kPageSize * page_multiplier_);
        read_oob_buffer_ = std::make_unique<uint8_t[]>(group_size_ * oob_size_ * page_multiplier_);

        write_page_buffer_ = std::make_unique<uint8_t[]>(
            group_size_ * kPageSize * page_multiplier_);
        write_oob_buffer_ = std::make_unique<uint8_t[]>(group_size_ * oob_size_ * page_multiplier_);

        auto intf = std::make_unique<FakeNandInterface>(kPageSize, oob_size_, kBlockSize, kSize);
        interface_ = intf.get();

        ASSERT_OK(NandVolumeDriver::Create(block_offset, kMaxBadBlocks, std::move(intf),
                                           &nand_volume_driver_));
        ASSERT_NULL(nand_volume_driver_->Init());
    }

    uint32_t PageBufferSize() {
        return group_size_ * kPageSize * page_multiplier_;
    }

    uint32_t OobBufferSize() {
        return group_size_ * oob_size_ * page_multiplier_;
    }

    void SetWritePageBufferData(uint8_t value) {
        memset(write_page_buffer_.get(), value, PageBufferSize());
    }

    void SetWriteOobBufferData(uint8_t value) {
        memset(write_oob_buffer_.get(), value, OobBufferSize());
    }

    void SetReadPageBufferData(uint8_t value) {
        memset(read_page_buffer_.get(), value, PageBufferSize());
    }

    void SetReadOobBufferData(uint8_t value) {
        memset(read_oob_buffer_.get(), value, OobBufferSize());
    }

    std::unique_ptr<uint8_t[]> read_page_buffer_;
    std::unique_ptr<uint8_t[]> read_oob_buffer_;
    std::unique_ptr<uint8_t[]> write_page_buffer_;
    std::unique_ptr<uint8_t[]> write_oob_buffer_;

    uint32_t page_multiplier_;
    uint32_t oob_size_;
    uint32_t group_size_;

    FakeNandInterface* interface_;
    std::unique_ptr<NandVolumeDriver> nand_volume_driver_;
};

TEST_F(NandVolumeDriverTest, WriteAllSucceeds) {
    // Start on block 2 (0-indexed).
    // Try to write all pages, 4 at a time.
    uint32_t block_offset = 2;
    uint32_t group_size = 4;

    SetUpDriver(block_offset, group_size, kOobSizeDefault);
    SetWritePageBufferData(0x12);
    SetWriteOobBufferData(0x89);

    uint32_t byte_offset = block_offset * kBlockSize;
    uint32_t num_pages = (kSize - byte_offset) / (page_multiplier_ * kPageSize);

    for (uint32_t page = 0; page < num_pages; page += group_size) {
        ASSERT_EQ(ftl::kNdmOk, nand_volume_driver_->NandWrite(page, group_size,
                                                              write_page_buffer_.get(),
                                                              write_oob_buffer_.get()));
    }

    for (uint32_t offset = byte_offset; offset < kSize; offset += kPageSize) {
        SetReadPageBufferData(0xFF);
        SetReadOobBufferData(0xFF);

        uint32_t actual;
        ASSERT_OK(interface_->ReadPage(offset, read_page_buffer_.get(), &actual));
        ASSERT_OK(interface_->ReadOob(offset, read_oob_buffer_.get()));

        ASSERT_BYTES_EQ(read_page_buffer_.get(), write_page_buffer_.get(), kPageSize);
        ASSERT_BYTES_EQ(read_oob_buffer_.get(), write_oob_buffer_.get(), oob_size_);
    }
}

TEST_F(NandVolumeDriverTest, WriteAllWithPageMultiplierSucceeds) {
    // Start on block 4 (0-indexed).
    // Try to write all pages, 2 at a time with page multiplier.
    uint32_t block_offset = 4;
    uint32_t group_size = 2;

    SetUpDriver(block_offset, group_size, kOobSizeNeedsMultiplier2);
    SetWritePageBufferData(0x01);
    SetWriteOobBufferData(0x78);

    uint32_t byte_offset = block_offset * kBlockSize;
    uint32_t num_pages = (kSize - byte_offset) / (page_multiplier_ * kPageSize);

    for (uint32_t page = 0; page < num_pages; page += group_size) {
        ASSERT_EQ(ftl::kNdmOk, nand_volume_driver_->NandWrite(page, group_size,
                                                              write_page_buffer_.get(),
                                                              write_oob_buffer_.get()));
    }

    for (uint32_t offset = byte_offset; offset < kSize; offset += kPageSize) {
        SetReadPageBufferData(0xFF);
        SetReadOobBufferData(0xFF);

        uint32_t actual;
        ASSERT_OK(interface_->ReadPage(offset, read_page_buffer_.get(), &actual));
        ASSERT_OK(interface_->ReadOob(offset, read_oob_buffer_.get()));

        ASSERT_BYTES_EQ(read_page_buffer_.get(), write_page_buffer_.get(), kPageSize);
        ASSERT_BYTES_EQ(read_oob_buffer_.get(), write_oob_buffer_.get(), oob_size_);
    }
}

TEST_F(NandVolumeDriverTest, BadWriteReportsError) {
    SetUpDriver(0, 1, kOobSizeDefault);

    // Attempt to write to non-existent page.
    ASSERT_EQ(ftl::kNdmFatalError, nand_volume_driver_->NandWrite(kSize, 1,
                                                                  write_page_buffer_.get(),
                                                                  write_oob_buffer_.get()));

    // Bad read from interface should result in an error.
    interface_->set_write_fails(true);
    ASSERT_EQ(ftl::kNdmError, nand_volume_driver_->NandWrite(0, 1, write_page_buffer_.get(),
                                                             write_oob_buffer_.get()));
}

TEST_F(NandVolumeDriverTest, ReadAllSucceeds) {
    // Start on block 16 (0-indexed).
    // Try to read all pages, 2 at a time.
    uint32_t block_offset = 16;
    uint32_t group_size = 2;

    SetUpDriver(block_offset, group_size, kOobSizeDefault);
    SetWritePageBufferData(0x23);
    SetWriteOobBufferData(0xA1);

    uint32_t byte_offset = block_offset * kBlockSize;
    for (uint32_t offset = byte_offset; offset < kSize; offset += kPageSize) {
        ASSERT_OK(interface_->WritePage(offset, write_page_buffer_.get(), write_oob_buffer_.get()));
    }

    uint32_t num_pages = (kSize - byte_offset) / (page_multiplier_ * kPageSize);
    for (uint32_t page = 0; page < num_pages; page += group_size) {
        SetReadPageBufferData(0xFF);
        SetReadOobBufferData(0xFF);

        ASSERT_EQ(ftl::kNdmOk, nand_volume_driver_->NandRead(page, group_size,
                                                             read_page_buffer_.get(),
                                                             read_oob_buffer_.get()));

        ASSERT_BYTES_EQ(write_page_buffer_.get(), read_page_buffer_.get(), PageBufferSize());
        ASSERT_BYTES_EQ(write_oob_buffer_.get(), read_oob_buffer_.get(), OobBufferSize());
    }
}

TEST_F(NandVolumeDriverTest, ReadAllWithPageMultiplierSucceeds) {
    // Start on block 1 (0-indexed).
    // Try to read all pages, 1 at a time with page multiplier.
    uint32_t block_offset = 1;
    uint32_t group_size = 1;

    SetUpDriver(block_offset, group_size, kOobSizeNeedsMultiplier2);
    SetWritePageBufferData(0xF0);
    SetWriteOobBufferData(0x6E);

    uint32_t byte_offset = block_offset * kBlockSize;
    for (uint32_t offset = byte_offset; offset < kSize; offset += kPageSize) {
        ASSERT_OK(interface_->WritePage(offset, write_page_buffer_.get(), write_oob_buffer_.get()));
    }

    uint32_t num_pages = (kSize - byte_offset) / (page_multiplier_ * kPageSize);
    for (uint32_t page = 0; page < num_pages; page += group_size) {
        SetReadPageBufferData(0xFF);
        SetReadOobBufferData(0xFF);

        ASSERT_EQ(ftl::kNdmOk, nand_volume_driver_->NandRead(page, group_size,
                                                             read_page_buffer_.get(),
                                                             read_oob_buffer_.get()));

        ASSERT_BYTES_EQ(write_page_buffer_.get(), read_page_buffer_.get(), PageBufferSize());
        ASSERT_BYTES_EQ(write_oob_buffer_.get(), read_oob_buffer_.get(), OobBufferSize());
    }
}

TEST_F(NandVolumeDriverTest, BadReadReportsFatalError) {
    SetUpDriver(0, 1, kOobSizeNeedsMultiplier2);

    // Attempt to read from non-existent page should fail fatally.
    ASSERT_EQ(ftl::kNdmFatalError,
              nand_volume_driver_->NandRead(kSize, 1, read_page_buffer_.get(),
                                            read_oob_buffer_.get()));

    // Bad read from interface should result in an error.
    interface_->set_read_fails(true);
    ASSERT_EQ(ftl::kNdmFatalError,
              nand_volume_driver_->NandRead(0, 1, read_page_buffer_.get(), nullptr));
    ASSERT_EQ(ftl::kNdmFatalError,
              nand_volume_driver_->NandRead(0, 1, nullptr, read_oob_buffer_.get()));
}

TEST_F(NandVolumeDriverTest, ShortReadReportsError) {
    SetUpDriver(0, 1, kOobSizeDefault);

    // Say no data was actually read.
    interface_->set_read_actual(0);
    ASSERT_EQ(ftl::kNdmFatalError,
              nand_volume_driver_->NandRead(0, 1, read_page_buffer_.get(), read_oob_buffer_.get()));
}

} // namespace
