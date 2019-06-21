// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <lib/ftl-mtd/ftl-volume-wrapper.h>
#include <lib/ftl-mtd/nand-volume-driver.h>
#include <lib/mtd/mtd-interface.h>
#include <zxtest/zxtest.h>

using namespace ftl_mtd;

// FtlVolumeWrapperIntegrationTest relies on a device file located at /dev/mtd0/
// On the host machine, nandsim is used to create a virtual MTD device.
// The following command was used to create the device for this test.
// $ sudo modprobe nandsim id_bytes=0x2c,0xdc,0x90,0xa6,0x54,0x0 badblocks=5

namespace {

constexpr const char* kTestDevicePath = "/dev/mtd0";
constexpr uint32_t kBlockOffset = 0;
constexpr uint32_t kMaxBadBlocks = 10;

class FtlVolumeWrapperIntegrationTest : public zxtest::Test {
protected:
    void SetUp() override {
        ftl_volume_wrapper_ = std::make_unique<FtlVolumeWrapper>();
        auto mtd_interface = mtd::MtdInterface::Create(kTestDevicePath);
        interface_ = mtd_interface.get();
        WipeNandInterface();

        std::unique_ptr<NandVolumeDriver> nand_volume_driver;
        ASSERT_OK(NandVolumeDriver::Create(kBlockOffset, kMaxBadBlocks, std::move(mtd_interface),
                                           &nand_volume_driver));

        ASSERT_NULL(nand_volume_driver->Init());
        ASSERT_OK(ftl_volume_wrapper_->Init(std::move(nand_volume_driver)));
    }

    void WipeNandInterface() {
        uint32_t size = interface_->Size();
        uint32_t block_size = interface_->BlockSize();

        for (uint32_t block_offset = 0; block_offset < size; block_offset += block_size) {
            bool is_bad_block;
            ASSERT_OK(interface_->IsBadBlock(block_offset, &is_bad_block));

            if (!is_bad_block) {
                ASSERT_OK(interface_->EraseBlock(block_offset));
            }
        }
    }

    mtd::NandInterface* interface_;
    std::unique_ptr<FtlVolumeWrapper> ftl_volume_wrapper_;
};

TEST_F(FtlVolumeWrapperIntegrationTest, ReadWriteSucceeds) {
    uint32_t page_count = 2;
    uint32_t byte_count = page_count * interface_->PageSize();
    uint32_t seek_offset = 15 * interface_->BlockSize();
    std::vector<uint8_t> buffer(byte_count);

    // Wrapper should always start at 0.
    ASSERT_EQ(0, ftl_volume_wrapper_->Tell());

    // 1. Write one set of values.
    memset(buffer.data(), 0xAB, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Write(buffer.data(), byte_count));
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Tell());

    // 2. Write a different set of values.
    memset(buffer.data(), 0xCD, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Write(buffer.data(), byte_count));
    ASSERT_EQ(2 * byte_count, ftl_volume_wrapper_->Tell());

    // 3. Seek to a different place and write yet another different set of values.
    ASSERT_EQ(seek_offset, ftl_volume_wrapper_->Seek(seek_offset, SEEK_SET));
    memset(buffer.data(), 0x1F, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Write(buffer.data(), byte_count));
    ASSERT_EQ(seek_offset + byte_count, ftl_volume_wrapper_->Tell());

    ASSERT_OK(ftl_volume_wrapper_->Sync());

    // Read back the bytes and ensure they're what we expect.
    ASSERT_EQ(0, ftl_volume_wrapper_->Seek(0, SEEK_SET));
    memset(buffer.data(), 0, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Read(buffer.data(), byte_count));
    ASSERT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t val) { return val == 0xAB; }));
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Tell());

    memset(buffer.data(), 0, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Read(buffer.data(), byte_count));
    ASSERT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t val) { return val == 0xCD; }));
    ASSERT_EQ(2 * byte_count, ftl_volume_wrapper_->Tell());

    ASSERT_EQ(seek_offset, ftl_volume_wrapper_->Seek(seek_offset, SEEK_SET));
    memset(buffer.data(), 0, byte_count);
    ASSERT_EQ(byte_count, ftl_volume_wrapper_->Read(buffer.data(), byte_count));
    ASSERT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t val) { return val == 0x1F; }));
    ASSERT_EQ(seek_offset + byte_count, ftl_volume_wrapper_->Tell());
}

} // namespace
