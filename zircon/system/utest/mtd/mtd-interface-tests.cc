// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include <lib/mtd/mtd-interface.h>

using namespace mtd;

// This MTD interface test relies on a device file located at /dev/mtd0/
// On the host machine, nandsim is used to create a virtual MTD device.
// The following command was used to create the device for this test.
// $ sudo modprobe nandsim id_bytes=0x2c,0xdc,0x90,0xa6,0x54,0x0 badblocks=5
// linux/arm64-based tests are run on astro.

namespace {

#ifdef ASTRO
constexpr const char* kTestDevicePath = "/dev/mtd/mtd9";
constexpr uint32_t kOobSize = 8;             // 8 bytes
constexpr uint32_t kPageSize = 4 * 1024;     // 4KiB
constexpr uint32_t kBlockSize = 256 * 1024;  // 256KiB
constexpr uint32_t kSize = 3 * 1024 * 1024;  // 3MiB
#else
constexpr const char* kTestDevicePath = "/dev/mtd0";
constexpr uint32_t kOobSize = 128;             // 128 bytes
constexpr uint32_t kPageSize = 4 * 1024;       // 4KiB
constexpr uint32_t kBlockSize = 256 * 1024;    // 256KiB
constexpr uint32_t kSize = 512 * 1024 * 1024;  // 512MiB
#endif

class MtdInterfaceTest : public zxtest::Test {
 protected:
  void SetUp() override { mtd_ = MtdInterface::Create(kTestDevicePath); }

  void SetData(int8_t data, uint32_t size) {
    data_ = std::vector<uint8_t>(size);
    memset(data_.data(), data, size);
  }

  void SetOob(int8_t data, uint32_t size) {
    oob_ = std::vector<uint8_t>(size);
    memset(oob_.data(), data, size);
  }

  std::unique_ptr<MtdInterface> mtd_;
  std::vector<uint8_t> data_;
  std::vector<uint8_t> oob_;
};

TEST_F(MtdInterfaceTest, ValidMtd) {
  ASSERT_NE(nullptr, mtd_, "Failed to initialize mtd_ with device %s", kTestDevicePath);

  // The following specifications are set by the chip ID and never change.
  EXPECT_EQ(kPageSize, mtd_->PageSize());
  EXPECT_EQ(kBlockSize, mtd_->BlockSize());
  EXPECT_EQ(kOobSize, mtd_->OobSize());
  EXPECT_EQ(kSize, mtd_->Size());
}

TEST_F(MtdInterfaceTest, InvalidMtd) {
  // File does not exist.
  std::unique_ptr<MtdInterface> nonexistant_mtd =
      MtdInterface::Create("/dev/bad/mtd");
  EXPECT_EQ(nullptr, nonexistant_mtd);

  // File is not an MTD device.
  std::unique_ptr<MtdInterface> invalid_mtd = MtdInterface::Create("/dev/zero");
  EXPECT_EQ(nullptr, invalid_mtd);
}

TEST_F(MtdInterfaceTest, ReadWriteEraseTest) {
  uint32_t bytes_read;
  std::vector<uint8_t> out_data(mtd_->PageSize());
  std::vector<uint8_t> out_oob(mtd_->OobSize());

  const uint32_t page20 = 20 * mtd_->PageSize();
  const uint32_t page5 = 5 * mtd_->PageSize();
  uint32_t block = page5 & ~(mtd_->BlockSize() - 1);

  // Erase the block containing page 5 then verify page 5 is empty.
  ASSERT_EQ(ZX_OK, mtd_->EraseBlock(block));
  ASSERT_EQ(ZX_OK, mtd_->ReadPage(page5, out_data.data(), &bytes_read));
  ASSERT_EQ(ZX_OK, mtd_->ReadOob(page5, out_oob.data()));
  ASSERT_EQ(mtd_->PageSize(), bytes_read);

  for (uint32_t i = 0; i < mtd_->PageSize(); i++) {
    ASSERT_EQ(0xFF, out_data[i], "Failed at index %u", i);
  }

  for (uint32_t i = 0; i < mtd_->OobSize(); i++) {
    ASSERT_EQ(0xFF, out_oob[i], "Failed at index %u", i);
  }

  // Set data/oob to be written.
  SetData(0x12, mtd_->PageSize());
  SetOob(0x23, mtd_->OobSize());

  // Write one page 20 then read.
  ASSERT_EQ(ZX_OK, mtd_->WritePage(page20, data_.data(), oob_.data()));
  ASSERT_EQ(ZX_OK, mtd_->ReadPage(page20, out_data.data(), &bytes_read));
  ASSERT_EQ(ZX_OK, mtd_->ReadOob(page20, out_oob.data()));
  ASSERT_EQ(mtd_->PageSize(), bytes_read);

  for (uint32_t i = 0; i < mtd_->PageSize(); i++) {
    ASSERT_EQ(data_[i], out_data[i], "Failed at index %u", i);
  }

  for (uint32_t i = 0; i < mtd_->OobSize(); i++) {
    ASSERT_EQ(oob_[i], out_oob[i], "Failed at index %u", i);
  }

  // Set data/oob with different bytes to be written.
  SetData(0x45, mtd_->PageSize());
  SetOob(0x67, mtd_->OobSize());

  // Write different data to page 5 then read and verify.
  ASSERT_EQ(ZX_OK, mtd_->WritePage(page5, data_.data(), oob_.data()));
  ASSERT_EQ(ZX_OK, mtd_->ReadPage(page5, out_data.data(), &bytes_read));
  ASSERT_EQ(ZX_OK, mtd_->ReadOob(page5, out_oob.data()));
  ASSERT_EQ(mtd_->PageSize(), bytes_read);

  for (uint32_t i = 0; i < mtd_->PageSize(); i++) {
    ASSERT_EQ(data_[i], out_data[i], "Failed at index %u", i);
  }

  for (uint32_t i = 0; i < mtd_->OobSize(); i++) {
    ASSERT_EQ(oob_[i], out_oob[i], "Failed at index %u", i);
  }

  // Read the page 20 again. Verify it hasn't changed.
  ASSERT_EQ(ZX_OK, mtd_->ReadPage(page20, out_data.data(), &bytes_read));
  ASSERT_EQ(ZX_OK, mtd_->ReadOob(page20, out_oob.data()));
  ASSERT_EQ(mtd_->PageSize(), bytes_read);

  // Set data/oob with same bytes expected for page 20.
  SetData(0x12, mtd_->PageSize());
  SetOob(0x23, mtd_->OobSize());

  for (uint32_t i = 0; i < mtd_->PageSize(); i++) {
    ASSERT_EQ(data_[i], out_data[i], "Failed at index %u", i);
  }

  for (uint32_t i = 0; i < mtd_->OobSize(); i++) {
    ASSERT_EQ(oob_[i], out_oob[i], "Failed at index %u", i);
  }

  // Erase the block containing page 5 then verify page 5 is empty.
  ASSERT_EQ(ZX_OK, mtd_->EraseBlock(block));
  ASSERT_EQ(ZX_OK, mtd_->ReadPage(page5, out_data.data(), &bytes_read));
  ASSERT_EQ(ZX_OK, mtd_->ReadOob(page5, out_oob.data()));
  ASSERT_EQ(mtd_->PageSize(), bytes_read);

  for (uint32_t i = 0; i < mtd_->PageSize(); i++) {
    ASSERT_EQ(0xFF, out_data[i], "Failed at index %u", i);
  }

  for (uint32_t i = 0; i < mtd_->OobSize(); i++) {
    ASSERT_EQ(0xFF, out_oob[i], "Failed at index %u", i);
  }
}

TEST_F(MtdInterfaceTest, InvalidOffset) {
  const uint32_t nonPageOffset = kPageSize - 1;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            mtd_->WritePage(nonPageOffset, nullptr, nullptr));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            mtd_->ReadPage(nonPageOffset, nullptr, nullptr));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, mtd_->EraseBlock(nonPageOffset));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, mtd_->IsBadBlock(nonPageOffset, nullptr));
}

// TODO(mbrunson): Figure out a way to run this test consistently on real
// hardware.
TEST_F(MtdInterfaceTest, BadBlockTest) {
  bool is_bad_block;

#ifndef ASTRO
  // nandsim with badblocks=5 should only mark pages in block 5 as bad.
  ASSERT_EQ(ZX_OK, mtd_->IsBadBlock(5 * mtd_->BlockSize(), &is_bad_block));
  EXPECT_TRUE(is_bad_block);
#endif

  ASSERT_EQ(ZX_OK, mtd_->IsBadBlock(0, &is_bad_block));
  EXPECT_FALSE(is_bad_block);
}

}  // namespace
