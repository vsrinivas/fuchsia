// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <time.h>

#include <array>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "ftl-shell.h"
#include "test/ndm-ram-driver.h"

namespace {

constexpr uint32_t kPageSize = 4096;

// 300 blocks of 64 pages.
constexpr ftl::VolumeOptions kDefaultOptions = {300, 300 / 20, 64 * kPageSize, kPageSize, 16, 0};

TEST(FtlTest, TrivialLifetime) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));
}

// See ReAttachTest for a non-trivial flush test.
TEST(FtlTest, TrivialFlush) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));
  ASSERT_OK(ftl.volume()->Flush());
}

bool IsEmptyPage(FtlShell* ftl, uint32_t page_num) {
  fbl::Array<uint8_t> buffer(new uint8_t[kPageSize], kPageSize);
  memset(buffer.data(), 0, buffer.size());

  if (ftl->volume()->Read(page_num, 1, buffer.data()) != ZX_OK) {
    return false;
  }

  for (uint32_t i = 0; i < buffer.size(); i++) {
    if (buffer[i] != 0xff) {
      return false;
    }
  }
  return true;
}

TEST(FtlTest, Unmount) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));
  ASSERT_OK(ftl.volume()->Unmount());
}

TEST(FtlTest, Mount) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));
  ASSERT_OK(ftl.volume()->Unmount());
  ASSERT_OK(ftl.volume()->Mount());

  ASSERT_TRUE(IsEmptyPage(&ftl, 10));
}

TEST(FtlTest, ReadWrite) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  fbl::Array<uint8_t> buffer(new uint8_t[kPageSize * 2], kPageSize * 2);
  memset(buffer.data(), 0x55, buffer.size());

  ASSERT_OK(ftl.volume()->Write(150, 2, buffer.data()));

  memset(buffer.data(), 0, buffer.size());
  ASSERT_OK(ftl.volume()->Read(150, 2, buffer.data()));

  for (uint32_t i = 0; i < buffer.size(); i++) {
    ASSERT_EQ(0x55, buffer[i]);
  }
}

zx_status_t WritePage(FtlShell* ftl, uint32_t page_num) {
  fbl::Array<uint8_t> buffer(new uint8_t[kPageSize], kPageSize);
  memset(buffer.data(), 0x55, buffer.size());

  return ftl->volume()->Write(page_num, 1, buffer.data());
}

TEST(FtlTest, ReAttach) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  fbl::Array<uint8_t> buffer(new uint8_t[kPageSize * 2], kPageSize * 2);
  memset(buffer.data(), 0x55, buffer.size());

  ASSERT_OK(ftl.volume()->Write(150, 2, buffer.data()));

  ASSERT_TRUE(ftl.ReAttach());
  ASSERT_TRUE(IsEmptyPage(&ftl, 150));

  // Try again, this time flushing before removing the volume.
  ASSERT_OK(ftl.volume()->Write(150, 2, buffer.data()));

  ASSERT_OK(ftl.volume()->Flush());
  ASSERT_TRUE(ftl.ReAttach());

  memset(buffer.data(), 0, buffer.size());
  ASSERT_OK(ftl.volume()->Read(150, 2, buffer.data()));

  for (uint32_t i = 0; i < buffer.size(); i++) {
    ASSERT_EQ(0x55, buffer[i]);
  }
}

TEST(FtlTest, ReAttachOverflowTest) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  // Each time we reattach, the global variable NdmSemCount would increase.
  // We need to make sure we do not overflow the sem_name variable.
  // See fxb/39772.
  for (int i = 0; i < 1000; i++) {
    ASSERT_TRUE(ftl.ReAttach());
  }
}

TEST(FtlTest, Format) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  ASSERT_OK(WritePage(&ftl, 10));
  ASSERT_OK(ftl.volume()->Format());

  ASSERT_TRUE(IsEmptyPage(&ftl, 10));
}

TEST(FtlTest, FormatAndLevel) {
  ftl::VolumeOptions options = kDefaultOptions;
  options.num_blocks = 30;
  options.block_size = 4 * kPageSize;

  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(options));

  // Remember what a leveled volume looks like.
  ftl::Volume::Stats stats;
  ASSERT_OK(ftl.volume()->GetStats(&stats));
  uint32_t last_bucket = stats.wear_histogram[fbl::count_of(stats.wear_histogram) - 1];

  for (uint32_t i = 0; i < ftl.num_pages(); i++) {
    ASSERT_OK(WritePage(&ftl, i));
  }

  for (int i = 0; i < 100; i++) {
    ASSERT_OK(WritePage(&ftl, 10));
  }

  ASSERT_OK(ftl.volume()->GetStats(&stats));
  ASSERT_NE(last_bucket, stats.wear_histogram[fbl::count_of(stats.wear_histogram) - 1]);

  ASSERT_OK(ftl.volume()->FormatAndLevel());

  ASSERT_OK(ftl.volume()->GetStats(&stats));
  ASSERT_EQ(last_bucket, stats.wear_histogram[fbl::count_of(stats.wear_histogram) - 1]);
  ASSERT_TRUE(IsEmptyPage(&ftl, 10));
}

TEST(FtlTest, Trim) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  ASSERT_OK(WritePage(&ftl, 10));
  ASSERT_OK(ftl.volume()->Trim(10, 1));

  ASSERT_TRUE(IsEmptyPage(&ftl, 10));
}

TEST(FtlTest, GarbageCollect) {
  FtlShell ftl;
  constexpr int kBlocks = 10;
  ASSERT_TRUE(ftl.Init({kBlocks, 1, 32 * kPageSize, kPageSize, 16, 0}));

  // Even though the device is empty, the FTL erases the blocks before use,
  // and for this API that counts as garbage collection.
  // Two reserved blocks + one that may become bad.
  for (int i = 0; i < kBlocks - 3; i++) {
    ASSERT_OK(ftl.volume()->GarbageCollect());
  }
  ASSERT_EQ(ZX_ERR_STOP, ftl.volume()->GarbageCollect());
}

TEST(FtlTest, Stats) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));

  ftl::Volume::Stats stats;
  ASSERT_OK(ftl.volume()->GetStats(&stats));
  ASSERT_EQ(0, stats.garbage_level);
  ASSERT_EQ(0, stats.wear_count);
  ASSERT_LT(0, stats.ram_used);
  ASSERT_LT(200, stats.num_blocks);
  for (size_t i = 0; i < fbl::count_of(stats.wear_histogram) - 1; i++) {
    ASSERT_EQ(0, stats.wear_histogram[i]);
  }
}

class FtlTest : public zxtest::Test {
 public:
  using PageCount = uint32_t;

  void SetUp() override;

  // Goes over a single iteration of the "main" ftl test. |write_size| is the
  // number of pages to write at the same time.
  void SingleLoop(PageCount write_size);

 protected:
  // Returns the value to use for when writing |page_num|.
  uint32_t GetKey(uint32_t page_num) { return (write_counters_[page_num] << 24) | page_num; }

  // Fills the page buffer with a known pattern for each page.
  void PrepareBuffer(uint32_t page_num, uint32_t write_size);

  void CheckVolume(uint32_t write_size, uint32_t total_pages);

  FtlShell ftl_;
  ftl::Volume* volume_ = nullptr;
  fbl::Array<uint8_t> write_counters_;
  fbl::Array<uint32_t> page_buffer_;
};

void FtlTest::SetUp() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  ASSERT_TRUE(ftl_.Init(kDefaultOptions));
  volume_ = ftl_.volume();
  ASSERT_OK(volume_->Unmount());

  write_counters_.reset(new uint8_t[ftl_.num_pages()], ftl_.num_pages());
  memset(write_counters_.data(), 0, write_counters_.size());
}

void FtlTest::SingleLoop(PageCount write_size) {
  ASSERT_OK(volume_->Mount());

  size_t buffer_size = write_size * ftl_.page_size() / sizeof(uint32_t);
  page_buffer_.reset(new uint32_t[buffer_size], buffer_size);
  memset(page_buffer_.data(), 0, page_buffer_.size() * sizeof(page_buffer_[0]));

  // Write pages 5 - 10.
  for (uint32_t page = 5; page < 10; page++) {
    ASSERT_OK(volume_->Write(page, 1, page_buffer_.data()));
  }

  // Mark pages 5 - 10 as unused.
  ASSERT_OK(volume_->Trim(5, 5));

  // Write every page in the volume once.
  for (uint32_t page = 0; page < ftl_.num_pages();) {
    uint32_t count = fbl::min(ftl_.num_pages() - page, write_size);
    PrepareBuffer(page, count);

    ASSERT_OK(volume_->Write(page, count, page_buffer_.data()));
    page += count;
  }

  ASSERT_OK(volume_->Flush());
  ASSERT_NO_FATAL_FAILURES(CheckVolume(write_size, ftl_.num_pages()));

  // Randomly rewrite half the pages in the volume.
  for (uint32_t i = 0; i < ftl_.num_pages() / 2; i++) {
    uint32_t page = static_cast<uint32_t>(rand() % ftl_.num_pages());
    PrepareBuffer(page, 1);

    ASSERT_OK(volume_->Write(page, 1, page_buffer_.data()));
  }

  ASSERT_NO_FATAL_FAILURES(CheckVolume(write_size, ftl_.num_pages()));

  // Detach and re-add test volume without erasing the media.
  ASSERT_OK(volume_->Unmount());
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_NO_FATAL_FAILURES(CheckVolume(write_size, ftl_.num_pages()));

  ASSERT_OK(volume_->Unmount());
}

void FtlTest::PrepareBuffer(uint32_t page_num, uint32_t write_size) {
  uint32_t* key_buffer = page_buffer_.data();

  for (; write_size; write_size--, page_num++) {
    write_counters_[page_num]++;
    uint32_t value = GetKey(page_num);

    // Fill page buffer with repetitions of its unique write value.
    for (uint32_t i = 0; i < ftl_.page_size() / sizeof(value); i++) {
      *key_buffer++ = value;
    }
  }
}

void FtlTest::CheckVolume(uint32_t write_size, uint32_t total_pages) {
  for (uint32_t page = 0; page < total_pages;) {
    uint32_t count = fbl::min(total_pages - page, write_size);
    ASSERT_OK(volume_->Read(page, count, page_buffer_.data()), "page %u", page);

    // Verify each page independently.
    uint32_t* key_buffer = page_buffer_.data();
    uint32_t* end = key_buffer + ftl_.page_size() / sizeof(uint32_t) * count;
    for (; key_buffer < end; page++) {
      // Get 32-bit data unique to most recent page write.
      uint32_t value = GetKey(page);
      for (size_t i = 0; i < ftl_.page_size(); i += sizeof(value), key_buffer++) {
        if (*key_buffer != value) {
          ASSERT_TRUE(false,
                      "Page #%u corrupted at offset %zu. Expected 0x%08X, "
                      "found 0x%08X\n",
                      page, i, value, *key_buffer);
        }
      }
    }
  }
}

TEST_F(FtlTest, SinglePass) { SingleLoop(5); }

TEST_F(FtlTest, MultiplePass) {
  for (int i = 1; i < 7; i++) {
    ASSERT_NO_FATAL_FAILURES(SingleLoop(i * 3), "i: %d", i);
  }
}

class FtlTestWithDriverAccess : public FtlTest {
 public:
  void SetUp() override {}

  // Performs the required steps so that an FtlTest method would see a volume
  // that matches the current state.
  void SetUpBaseTest();
};

void FtlTestWithDriverAccess::SetUpBaseTest() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  volume_ = ftl_.volume();
  ASSERT_OK(volume_->Unmount());

  write_counters_.reset(new uint8_t[ftl_.num_pages()], ftl_.num_pages());
  memset(write_counters_.data(), 0, write_counters_.size());
}

using FtlExtendTest = FtlTestWithDriverAccess;

TEST_F(FtlExtendTest, ExtendVolume) {
  TestOptions driver_options = {};
  driver_options.use_half_size = true;
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, driver_options);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_EQ(driver->Init(), nullptr);
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // Start by writing to the "small" volume.

  const int kWriteSize = 5;
  uint32_t original_size = ftl_.num_pages();
  ASSERT_NO_FATAL_FAILURES(SingleLoop(kWriteSize));

  // Double the volume size.

  ASSERT_TRUE(driver->Detach());
  ASSERT_TRUE(driver->DoubleSize());
  ASSERT_TRUE(ftl_.ReAttach());

  // Verify the contents of the first half of the volume.
  ASSERT_NO_FATAL_FAILURES(CheckVolume(kWriteSize, original_size));

  // Now make sure the whole volume works as expected.
  SetUpBaseTest();
  EXPECT_GT(ftl_.num_pages(), original_size);
  ASSERT_NO_FATAL_FAILURES(SingleLoop(kWriteSize));
}

TEST_F(FtlExtendTest, ReduceReservedBlocks) {
  TestOptions driver_options = {};
  driver_options.bad_block_interval = 500000;  // Large enough to avoid it.
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, driver_options);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_EQ(driver->Init(), nullptr);
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // Start by writing to the regular volume.
  const int kWriteSize = 5;
  uint32_t original_size = ftl_.num_pages();
  ASSERT_NO_FATAL_FAILURES(SingleLoop(kWriteSize));

  // Reduce the number of reserved blocks.
  driver->set_max_bad_blocks(kDefaultOptions.max_bad_blocks / 2);
  ASSERT_TRUE(ftl_.ReAttach());

  // Verify the contents of the first part of the volume.
  ASSERT_NO_FATAL_FAILURES(CheckVolume(kWriteSize, original_size));

  // Now make sure the whole volume works as expected.
  SetUpBaseTest();
  EXPECT_GT(ftl_.num_pages(), original_size);
  ASSERT_NO_FATAL_FAILURES(SingleLoop(kWriteSize));
}

TEST_F(FtlExtendTest, ReduceReservedBlocksFailure) {
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_EQ(driver->Init(), nullptr);
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // Start by writing to the regular volume.
  const int kWriteSize = 5;
  ASSERT_NO_FATAL_FAILURES(SingleLoop(kWriteSize));

  // Reduce the number of reserved blocks.
  driver->set_max_bad_blocks(kDefaultOptions.max_bad_blocks / 2);
  ASSERT_FALSE(ftl_.ReAttach());
}

// Reducing the bad block reservation should fail if it cannot hold the current
// bad block table.
TEST(FtlExtendTest, ReduceReservedBlocksTooSmall) {
  TestOptions driver_options = {};
  driver_options.bad_block_interval = 5;
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, driver_options);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  FtlShell ftl;
  ASSERT_EQ(driver->Init(), nullptr);
  ASSERT_TRUE(ftl.InitWithDriver(std::move(driver_to_pass)));

  // Generate enough activity to fill the bad block table.
  for (uint32_t page = 0; page < 50; page++) {
    ASSERT_OK(WritePage(&ftl, page));
  }
  ASSERT_OK(ftl.volume()->Unmount());
  ASSERT_TRUE(driver->Detach());

  // Reduce the number of reserved blocks: the table doesn't fit anymore.

  ftl::VolumeOptions options = kDefaultOptions;
  options.max_bad_blocks /= 2;
  ASSERT_GT(driver->num_bad_blocks(), options.max_bad_blocks);
  ASSERT_TRUE(driver->IsNdmDataPresent(options));
  ASSERT_TRUE(driver->BadBbtReservation());
}

// Even if the new table can hold the current one, if a translated block would
// end up in the wrong region the operation should fail.
TEST(FtlExtendTest, ReduceReservedBlocksInvalidLocation) {
  TestOptions driver_options = {};
  driver_options.bad_block_interval = 5;
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, driver_options);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_EQ(driver->Init(), nullptr);
  FtlShell ftl;
  ASSERT_TRUE(ftl.InitWithDriver(std::move(driver_to_pass)));

  // At this point a single write will be enough to generate a bad block.
  ASSERT_OK(WritePage(&ftl, 0));
  ASSERT_OK(ftl.volume()->Unmount());

  // Reduce the number of reserved blocks.
  ASSERT_TRUE(driver->Detach());

  ftl::VolumeOptions options = kDefaultOptions;
  options.max_bad_blocks /= 2;
  ASSERT_LT(driver->num_bad_blocks(), options.max_bad_blocks);
  ASSERT_TRUE(driver->IsNdmDataPresent(options));
  ASSERT_TRUE(driver->BadBbtReservation());
}

TEST(FtlTest, WearCountDistribution) {
  ftl::VolumeOptions options = kDefaultOptions;
  options.num_blocks = 117;  // Should end in 100 usable blocks.

  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(options));

  for (int i = 0; i < 40; i++) {
    // Fill the entire FTL.
    for (uint32_t page = 0; page < ftl.num_pages(); page++) {
      ASSERT_OK(WritePage(&ftl, page));
    }

    for (int j = 0; j < 30; j++) {
      // Keep writing to 20% of the disk.
      for (uint32_t page = 0; page < ftl.num_pages() / 5; page++) {
        ASSERT_OK(WritePage(&ftl, page));
      }
    }
  }

  ftl::Volume::Stats stats;
  ASSERT_OK(ftl.volume()->GetStats(&stats));
  EXPECT_EQ(100, stats.num_blocks);

  // Verify that none of the buckets close to get too behind (the lower 25% of
  // the histogram) is accumulating too many blocks.
  int close_to_fall_of = 0;
  for (int bucket = 0; bucket < 5; bucket++) {
    // If the distribution is flat, no bucket should have more than 5 blocks.
    EXPECT_GT(5, stats.wear_histogram[bucket], "Bucket %d too big", bucket);

    close_to_fall_of += stats.wear_histogram[bucket];
  }
  // In aggregate, the tail must be better than flat.
  EXPECT_GT(15, close_to_fall_of);
}

// These tests rely on internal data of the NDM and the actual behavior of the
// code (like where each header is saved), which is not ideal, but it's required
// to make sure things work as expected.
using FtlUpgradeTest = FtlTestWithDriverAccess;

// Verifies that the NDM header at page |page_num| matches the desired version.
// |buffer| is a scratch buffer to read the page.
void CheckNdmHeaderVersion(NdmRamDriver* driver, uint32_t page_num, uint16_t major, uint16_t minor,
                           void* buffer) {
  struct Header {
    uint16_t major_version;
    uint16_t minor_version;
  };
  auto header = reinterpret_cast<Header*>(buffer);
  ASSERT_EQ(0, driver->NandRead(page_num, 1, buffer, nullptr));
  EXPECT_EQ(major, header->major_version);
  EXPECT_EQ(minor, header->minor_version);
}

// Verifies that the NDM control header can be upgraded to version 2.
TEST_F(FtlUpgradeTest, UpgradesToVersion2) {
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, TestOptions::NoEccErrors());

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_NULL(driver->Init());
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // Do a pass using the old format (default).
  const int kWriteSize = 5;
  ASSERT_NO_FAILURES(SingleLoop(kWriteSize));

  // The test went through 15 bad blocks so there should be 30 control blocks.
  const uint32_t kControlPage0 = 299 * 64;  // First page of last block.
  const uint32_t kControlPage1 = 298 * 64;  // First page of previous block.
  void* buffer = page_buffer_.data();
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0, 1, 1, buffer));
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0 + 30, 1, 1, buffer));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage0 + 31, 1, buffer, nullptr));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage1, 1, buffer, nullptr));

  // Now use the new partition format.
  driver->save_config_data(true);
  ASSERT_TRUE(ftl_.ReAttach());

  // Verify the contents of the volume.
  ASSERT_NO_FATAL_FAILURES(CheckVolume(kWriteSize, ftl_.num_pages()));

  // Verify that the volume is usable and that reading the new format from disk works.
  ASSERT_OK(volume_->Unmount());
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage1, 2, 0, page_buffer_.data()));
  ASSERT_NO_FAILURES(SingleLoop(kWriteSize));

  // Only one new control block must be present.
  buffer = page_buffer_.data();
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0, 1, 1, buffer));
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0 + 30, 1, 1, buffer));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage0 + 31, 1, buffer, nullptr));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage1 + 1, 1, buffer, nullptr));
}

TEST_F(FtlUpgradeTest, CreateNewVolumeWithVersion2ByDefault) {
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions);

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_NULL(driver->Init());
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // This should result in two control blocks written: on for the basic data,
  // followed by one with the partition data.

  const uint32_t kControlPage0 = 299 * 64;  // First page of last block.
  const uint32_t kControlPage1 = 298 * 64;  // First page of previous block.
  std::array<char, kPageSize> buffer;
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0, 2, 0, buffer.data()));
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0 + 1, 2, 0, buffer.data()));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc,
            driver->NandRead(kControlPage0 + 2, 1, buffer.data(), nullptr));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage1, 1, buffer.data(), nullptr));

  // Verify that no new control blocks are created after restart.
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_OK(volume_->Unmount());
  ASSERT_EQ(ftl::kNdmUncorrectableEcc,
            driver->NandRead(kControlPage0 + 2, 1, buffer.data(), nullptr));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(kControlPage1, 1, buffer.data(), nullptr));
}

// Verifies that a new control block with partition data is automatically added.
TEST_F(FtlUpgradeTest, ForceUpgrade) {
  // Start with an old version.
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, TestOptions::NoEccErrors());

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_NULL(driver->Init());
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  ftl::VolumeOptions options = kDefaultOptions;
  options.flags |= ftl::kReadOnlyInit;

  driver->save_config_data(true);
  driver->set_options(options);
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_OK(volume_->Unmount());

  // Even with read-only initialization the partition data was saved.
  const uint32_t kControlPage0 = 299 * 64;  // First page of last block.
  const uint32_t kControlPage1 = 298 * 64;  // First page of previous block.
  std::array<char, kPageSize> buffer;
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0, 1, 1, buffer.data()));
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage1, 2, 0, buffer.data()));

  // Verify that only one new control block is created.
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_OK(volume_->Unmount());
  ASSERT_EQ(ftl::kNdmUncorrectableEcc,
            driver->NandRead(kControlPage0 + 1, 1, buffer.data(), nullptr));
  ASSERT_EQ(ftl::kNdmUncorrectableEcc,
            driver->NandRead(kControlPage1 + 1, 1, buffer.data(), nullptr));
}

TEST_F(FtlUpgradeTest, BadBlocksWriteVersion2) {
  // Start with an old version.
  auto driver_to_pass = std::make_unique<NdmRamDriver>(kDefaultOptions, TestOptions::NoEccErrors());

  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_NULL(driver->Init());
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  driver->save_config_data(true);
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_OK(volume_->Unmount());

  // Do a pass to force some bad blocks.
  const int kWriteSize = 5;
  ASSERT_NO_FAILURES(SingleLoop(kWriteSize));

  const uint32_t kControlPage0 = 299 * 64;  // First page of last block.
  const uint32_t kControlPage1 = 298 * 64;  // First page of previous block.
  std::array<char, kPageSize> buffer;
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage0, 1, 1, buffer.data()));
  ASSERT_NO_FAILURES(CheckNdmHeaderVersion(driver, kControlPage1, 2, 0, buffer.data()));
}

using FtlBadBlockTest = FtlTestWithDriverAccess;

TEST_F(FtlBadBlockTest, FtlSucceedsAfterContinousFailures) {
  auto options = kDefaultOptions;
  options.max_bad_blocks = 10;
  TestOptions driver_options = {};
  driver_options.bad_block_interval = 5;
  driver_options.bad_block_burst = 10;

  auto driver_to_pass = std::make_unique<NdmRamDriver>(options, driver_options);
  // Retain a pointer. The driver's lifetime is tied to ftl_.
  NdmRamDriver* driver = driver_to_pass.get();
  ASSERT_NULL(driver->Init());
  ASSERT_TRUE(ftl_.InitWithDriver(std::move(driver_to_pass)));
  SetUpBaseTest();

  // One page at a time.
  SingleLoop(1);

  ASSERT_NO_FATAL_FAILURES(CheckVolume(1, ftl_.num_pages()));
  ASSERT_TRUE(ftl_.ReAttach());
  ASSERT_NO_FATAL_FAILURES(CheckVolume(1, ftl_.num_pages()));
}

}  // namespace
