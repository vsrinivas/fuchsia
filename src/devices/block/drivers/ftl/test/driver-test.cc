// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "ndm-ram-driver.h"

namespace {

constexpr uint32_t kPagesPerBlock = 32;
constexpr uint32_t kPageSize = 2048;
constexpr uint32_t kOobSize = 16;

// 20 blocks of 32 pages, 4 bad blocks max.
constexpr ftl::VolumeOptions kDefaultOptions = {
    20, 4, (kPagesPerBlock * kPageSize), kPageSize, kOobSize, 0,
};

TEST(DriverTest, TrivialLifetime) { NdmRamDriver driver({}); }

// Basic smoke tests for NdmRamDriver:

TEST(DriverTest, ReadWrite) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());

  fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

  memset(data.data(), 0x55, data.size());
  memset(oob.data(), 0x66, oob.size());

  ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(5, 2, data.data(), oob.data()));

  memset(data.data(), 0, data.size());
  memset(oob.data(), 0, oob.size());
  ASSERT_EQ(ftl::kNdmOk, driver.NandRead(5, 2, data.data(), oob.data()));

  for (uint32_t i = 0; i < data.size(); i++) {
    ASSERT_EQ(0x55, data[i]);
  }

  for (uint32_t i = 0; i < oob.size(); i++) {
    ASSERT_EQ(0x66, oob[i]);
  }
}

// Writes a fixed pattern to the desired page.
bool WritePage(NdmRamDriver* driver, uint32_t page_num) {
  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
  memset(data.data(), 0x55, data.size());
  memset(oob.data(), 0, oob.size());

  return driver->NandWrite(page_num, 1, data.data(), oob.data()) == ftl::kNdmOk;
}

TEST(DriverTest, IsEmpty) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());

  // Use internal driver meta-data.
  ASSERT_TRUE(driver.IsEmptyPage(0, nullptr, nullptr));

  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
  memset(data.data(), 0x55, data.size());
  memset(oob.data(), 0, oob.size());
  ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(0, 1, data.data(), oob.data()));

  // Look at both meta-data and buffers.
  ASSERT_FALSE(driver.IsEmptyPage(0, data.data(), oob.data()));

  memset(data.data(), 0xff, data.size());
  memset(oob.data(), 0xff, oob.size());

  ASSERT_TRUE(driver.IsEmptyPage(0, data.data(), oob.data()));
}

TEST(DriverTest, Erase) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());

  ASSERT_TRUE(WritePage(&driver, 0));

  ASSERT_EQ(ftl::kNdmOk, driver.NandErase(0));
  ASSERT_TRUE(driver.IsEmptyPage(0, nullptr, nullptr));
}

TEST(DriverTest, IsBadBlock) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());

  ASSERT_EQ(ftl::kFalse, driver.IsBadBlock(0));

  ASSERT_TRUE(WritePage(&driver, 0));
  ASSERT_EQ(ftl::kTrue, driver.IsBadBlock(0));
}

TEST(DriverTest, CreateVolume) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());
  EXPECT_TRUE(driver.IsNdmDataPresent(kDefaultOptions));
  ASSERT_EQ(nullptr, driver.Attach(nullptr));
  ASSERT_TRUE(driver.Detach());
}

TEST(DriverTest, CreateVolumeReadOnly) {
  ASSERT_TRUE(ftl::InitModules());

  ftl::VolumeOptions options = kDefaultOptions;
  options.flags = ftl::kReadOnlyInit;

  NdmRamDriver driver(options);
  ASSERT_EQ(nullptr, driver.Init());
  EXPECT_FALSE(driver.IsNdmDataPresent(options));
  ASSERT_NE(nullptr, driver.Attach(nullptr));
}

TEST(DriverTest, ReAttach) {
  ASSERT_TRUE(ftl::InitModules());

  NdmRamDriver driver(kDefaultOptions);
  ASSERT_EQ(nullptr, driver.Init());
  ASSERT_EQ(nullptr, driver.Attach(nullptr));

  ASSERT_TRUE(WritePage(&driver, 5));

  ASSERT_TRUE(driver.Detach());
  ASSERT_EQ(nullptr, driver.Attach(nullptr));

  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
  ASSERT_EQ(ftl::kNdmOk, driver.NandRead(5, 1, data.data(), oob.data()));

  ASSERT_FALSE(driver.IsEmptyPage(5, data.data(), oob.data()));
}

// NdmRamDriver is supposed to inject failures periodically. This tests that it
// does.
TEST(DriverTest, WriteBadBlock) {
  ASSERT_TRUE(ftl::InitModules());

  TestOptions driver_options = kDefaultTestOptions;
  driver_options.bad_block_interval = 80;

  NdmRamDriver driver(kDefaultOptions, driver_options);
  ASSERT_EQ(nullptr, driver.Init());

  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);

  memset(data.data(), 0, data.size());
  memset(oob.data(), 0, oob.size());

  // Check that we cycle through bad block intervals.
  uint32_t page = 0;
  for (uint32_t cycle = 0; cycle < kDefaultOptions.max_bad_blocks; cycle++) {
    for (int i = 0; i < driver_options.bad_block_interval; i++) {
      ASSERT_EQ(ftl::kNdmOk, driver.NandErase(page), "Cycle: %d Interval: %d\n", cycle, i);
    }

    for (int i = 0; i < driver_options.bad_block_burst; i++, page += kPagesPerBlock) {
      ASSERT_EQ(ftl::kNdmError, driver.NandWrite(page, 1, data.data(), oob.data()),
                "Cycle: %d Interval: %d\n", cycle, i);
    }
  }
}

TEST(DriverTest, WriteBadBlockWithRange) {
  ASSERT_TRUE(ftl::InitModules());

  constexpr uint64_t kCycles = 5;

  TestOptions driver_options = kDefaultTestOptions;
  driver_options.bad_block_interval = 80;
  driver_options.bad_block_burst = kDefaultOptions.max_bad_blocks;

  auto options = kDefaultOptions;
  options.max_bad_blocks = kDefaultOptions.max_bad_blocks * kCycles;
  NdmRamDriver driver(options, driver_options);
  ASSERT_EQ(nullptr, driver.Init());

  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);

  memset(data.data(), 0, data.size());
  memset(oob.data(), 0, oob.size());

  // Check that we cycle through bad block intervals.=
  uint32_t page = 0;
  for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
    for (int i = 0; i < driver_options.bad_block_interval; i++) {
      ASSERT_EQ(ftl::kNdmOk, driver.NandErase(page), "Cycle: %d Interval: %d\n", cycle, i);
    }

    for (int i = 0; i < driver_options.bad_block_burst; i++, page += kPagesPerBlock) {
      ASSERT_EQ(ftl::kNdmError, driver.NandWrite(page, 1, data.data(), oob.data()),
                "Cycle: %d Interval: %d\n", cycle, i);
    }
  }
}

// NdmRamDriver is supposed to inject failures periodically. This tests that it
// does.
TEST(DriverTest, ReadUnsafeEcc) {
  ASSERT_TRUE(ftl::InitModules());

  TestOptions driver_options = kDefaultTestOptions;
  driver_options.ecc_error_interval = 80;

  NdmRamDriver driver(kDefaultOptions, driver_options);
  ASSERT_EQ(nullptr, driver.Init());

  fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
  fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);

  memset(data.data(), 0, data.size());
  memset(oob.data(), 0, oob.size());
  ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(0, 1, data.data(), oob.data()));

  for (int i = 0; i < driver_options.ecc_error_interval; i++) {
    ASSERT_EQ(ftl::kNdmOk, driver.NandRead(0, 1, data.data(), oob.data()));
  }

  ASSERT_EQ(ftl::kNdmUnsafeEcc, driver.NandRead(0, 1, data.data(), oob.data()));
  ASSERT_EQ(ftl::kNdmOk, driver.NandRead(0, 1, data.data(), oob.data()));
}

}  // namespace
