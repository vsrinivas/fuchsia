// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl/ndm-driver.h>
#include <string.h>

#include <gtest/gtest.h>

#include "src/devices/block/drivers/ftl/tests/ftl-shell.h"
#include "src/devices/block/drivers/ftl/tests/ndm-ram-driver.h"
#include "zircon/system/ulib/ftl/ftln/ftlnp.h"

namespace {

constexpr uint32_t kSpareSize = 16;
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPagesPerBlock = 64;

// 50 blocks means 3200 pages, which is enough to have several map pages.
constexpr ftl::VolumeOptions kDefaultOptions = {50,        2,          kPageSize* kPagesPerBlock,
                                                kPageSize, kSpareSize, 0};
// Don't sprinkle in errors by default.
constexpr TestOptions kBoringTestOptions = {-1, -1, 0, false, true, -1, false, std::nullopt};

TEST(FtlTest, IncompleteWriteWithValidity) {
  uint8_t spare[kSpareSize];
  memset(spare, 0xff, kSpareSize);
  FtlnSetSpareValidity(spare);
  ASSERT_FALSE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteWithBadValidity) {
  uint8_t spare[kSpareSize];
  memset(spare, 0xff, kSpareSize);
  spare[14] = 0;
  ASSERT_TRUE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteNoValidityBadWearCount) {
  uint8_t spare[kSpareSize];
  memset(spare, 0xff, kSpareSize);
  ASSERT_TRUE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteNoValidityGoodWearCount) {
  uint8_t spare[kSpareSize];
  memset(spare, 0xff, kSpareSize);
  spare[10] = 0;
  ASSERT_FALSE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, WriteRemountRead) {
  FtlShell ftl;
  ASSERT_TRUE(ftl.Init(kDefaultOptions));
  ftl::Volume* volume = ftl.volume();
  uint8_t buf[kPageSize];
  memcpy(buf, "abc123", 6);
  ASSERT_EQ(ZX_OK, volume->Write(1, 1, buf));
  ASSERT_EQ(ZX_OK, volume->Flush());
  ASSERT_EQ(nullptr, volume->ReAttach());
  uint8_t buf2[kPageSize];
  ASSERT_EQ(ZX_OK, volume->Read(1, 1, buf2));
  ASSERT_EQ(0, memcmp(buf, buf2, kPageSize));
}

// Test powercuts on map block transfer.
TEST(FtlTest, PowerCutOnBlockTransfer) {
  FtlShell ftl_shell;
  auto driver_owned = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  ASSERT_EQ(nullptr, driver_owned->Init());
  NdmRamDriver* driver_unowned = driver_owned.get();
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver_owned)));
  ftl::Volume* volume = ftl_shell.volume();

  // Do a normal write + flush.
  uint8_t buf[kPageSize];
  memcpy(buf, "abc123", 6);
  ASSERT_EQ(ZX_OK, volume->Write(0, 1, buf));
  ASSERT_EQ(ZX_OK, volume->Flush());

  // Get the page number of where the map page was just written.
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
  uint32_t phys_map_page = ftl->mpns[0];
  // Verify that it is not unmapped.
  ASSERT_NE(0xFFFFFFFFu, phys_map_page);

  // Test increasingly delayed power cuts until the transfer completes.
  uint32_t new_phys_map_page = phys_map_page;
  int power_cut_delay = -1;
  while (new_phys_map_page == phys_map_page) {
    driver_unowned->SetPowerFailureDelay(++power_cut_delay);

    // This is expected to fail many times.
    FtlnRecycleMapBlk(ftl, phys_map_page / kPagesPerBlock);

    // Re-enable power.
    driver_unowned->SetPowerFailureDelay(-1);

    // Reattach and grab new ftln and new location of the map page.
    ASSERT_EQ(nullptr, volume->ReAttach());
    ftl = reinterpret_cast<FTLN>(
        reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
    new_phys_map_page = ftl->mpns[0];
    // Verify that it is not unmapped.
    ASSERT_NE(0xFFFFFFFFu, new_phys_map_page);
  }
  // This should never succeed on the first try, since it prevents any reads or writes.
  ASSERT_LT(0, power_cut_delay);
}

// Poor ECC results in block migration due only to reads.
TEST(FtlTest, MigrateOnDangerousEcc) {
  FtlShell ftl_shell;
  auto driver_owned = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  ASSERT_EQ(nullptr, driver_owned->Init());
  NdmRamDriver* driver_unowned = driver_owned.get();
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver_owned)));
  ftl::Volume* volume = ftl_shell.volume();

  // Do a normal write for an entire volume block.
  uint8_t buf[kPageSize];
  for (uint32_t i = 0; i < kPagesPerBlock; ++i) {
    memcpy(buf, &i, sizeof(uint32_t));
    ASSERT_EQ(ZX_OK, volume->Write(i, 1, buf));
  }
  // Recreate the original page in the buffer for later comparisons.
  {
    uint32_t tmp = 0;
    memcpy(buf, &tmp, sizeof(uint32_t));
  }

  // The next write should be in a different volume block than the first write.
  uint8_t buf2[kPageSize];
  memcpy(buf2, "xzy789", 6);
  ASSERT_EQ(ZX_OK, volume->Write(kPagesPerBlock, 1, buf2));
  // Flush it all to disk.
  ASSERT_EQ(ZX_OK, volume->Flush());

  // Check the current location of the first written page. This is only reliable because the test
  // options have disabled simulating random bad blocks.
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
  uint32_t phys_page = -1;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, 0, &phys_page));
  ASSERT_NE(-1u, phys_page);

  // Set it to have poor ECC and read it back to flag the need for recycle.
  driver_unowned->SetUnsafeEcc(phys_page, true);
  ASSERT_EQ(ZX_OK, volume->Read(0, 1, buf2));
  ASSERT_EQ(0, memcmp(buf, buf2, kPageSize));

  // Nothing has changed. (yet)
  uint32_t new_phys_page = -1;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, 0, &new_phys_page));
  ASSERT_EQ(phys_page, new_phys_page);

  // Any read or write should trigger a recycle here on the block that needs it. So read something
  // completely unrelated in a different block.
  ASSERT_EQ(ZX_OK, volume->Read(kPagesPerBlock, 1, buf2));

  // Check the new location of page 0 as it should have migrated.
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, 0, &new_phys_page));
  ASSERT_NE(phys_page, new_phys_page);

  // Verify it is still intact.
  ASSERT_EQ(ZX_OK, volume->Read(0, 1, buf2));
  ASSERT_EQ(0, memcmp(buf, buf2, kPageSize));
}

// Simulate when page is partially written on an ECC boundary, allowing it to appear valid.
// This shouldn't be a real scenario that matters except that we use the OobDoubler class
// that masks this possibility for both the upper and lower layers.
TEST(FtlTest, PartialPageWriteRecovery) {
  FtlShell ftl_shell;
  auto driver_owned = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  ASSERT_EQ(nullptr, driver_owned->Init());
  NdmRamDriver* driver_unowned = driver_owned.get();
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver_owned)));
  ftl::Volume* volume = ftl_shell.volume();

  // Write some data to the tail end of a map page.
  uint8_t buf[kPageSize];
  memcpy(buf, "abc123", 6);
  uint32_t page;
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
  page = ftl->mappings_per_mpg - 1;
  ASSERT_EQ(ZX_OK, volume->Write(page, 1, buf));
  ASSERT_EQ(ZX_OK, volume->Flush());

  // Write some data to another page indexed by the same map page.
  uint8_t buf2[kPageSize];
  memcpy(buf2, "xyz789", 6);
  ASSERT_EQ(ZX_OK, volume->Write(0, 1, buf2));
  ASSERT_EQ(ZX_OK, volume->Flush());

  // Find the physical location of this map page, erase the ending of it including the spare since
  // it is normally spread along the ECC pages of the nand. This should throw away the other
  // written page as we're simulating an incomplete write of the page.
  memset(&driver_unowned->MainData(ftl->mpns[0])[kPageSize / 2], -1, kPageSize / 2);
  memset(&driver_unowned->SpareData(ftl->mpns[0])[kSpareSize / 2], -1, kSpareSize / 2);

  // Remount with the corruption.
  volume->ReAttach();

  // Verify the original page is intact.
  ASSERT_EQ(ZX_OK, volume->Read(page, 1, buf2));
  ASSERT_EQ(0, memcmp(buf, buf2, kPageSize));

  // We should have lost the second write flush and expect erase data for the other page.
  ASSERT_EQ(ZX_OK, volume->Read(0, 1, buf2));
  for (unsigned char& c : buf2) {
    ASSERT_EQ(0xFFu, c);
  }
}

}  // namespace
