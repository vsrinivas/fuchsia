// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <gtest/gtest.h>

#include "src/devices/block/drivers/ftl/tests/ftl-shell.h"
#include "src/devices/block/drivers/ftl/tests/ndm-ram-driver.h"
#include "src/storage/lib/ftl/ftln/ftlnp.h"
#include "src/storage/lib/ftl/ftln/ndm-driver.h"

namespace {

constexpr uint32_t kInvalidPage = static_cast<uint32_t>(-1);

constexpr uint32_t kSpareSize = 16;
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPagesPerBlock = 64;

// 50 blocks means 3200 pages, which is enough to have several map pages.
constexpr ftl::VolumeOptions kDefaultOptions = {.num_blocks = 50,
                                                .max_bad_blocks = 2,
                                                .block_size = kPageSize * kPagesPerBlock,
                                                .page_size = kPageSize,
                                                .eb_size = kSpareSize,
                                                .flags = 0};

// Don't sprinkle in errors by default.
constexpr TestOptions kBoringTestOptions = {
    .ecc_error_interval = -1,
    .bad_block_interval = -1,
    .bad_block_burst = 0,
    .use_half_size = false,
    .save_config_data = true,
    .power_failure_delay = -1,
    .emulate_half_write_on_power_failure = false,
    .ftl_logger = std::nullopt,
};

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

// Demonstrate how ECC failures part way through a map block can lead to permanent data loss
// due to preemptive recycling of free map pages during initialization.
//
// We set up the FTL such that Map Block 0 = [mpn0, mpn1, mpn0, mpn1 ...] and Map Block 1 = [mpn0].
// We then set an ECC failure on the first page in map block 0 (mpn1) which causes build_map to stop
// processing map block 0 (and only has mpn0 at that point). Once map block 1 is processed, there
// are no current mappings in map block 0 from the FTL's perspective, and thus it is preemptively
// erased in `init_ftln` after `build_map` returns.
TEST(FtlTest, MapPageEccFailure) {
  FtlShell ftl_shell;
  auto driver_owned = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  ASSERT_EQ(nullptr, driver_owned->Init());
  NdmRamDriver* driver_unowned = driver_owned.get();
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver_owned)));
  ftl::Volume* volume = ftl_shell.volume();
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());

  uint8_t buf[kPageSize];
  memcpy(buf, "abc123", 6);

  constexpr uint32_t kMappingsPerMpn = kPageSize / 4;

  // 1. Consume the first map block by writing out kPagesPerBlock pages to pages spanning MPN0/1.
  // 2. Consume first page of a new map block by writing to MPN0 (ensure the VPN is one
  // that we wrote in the first map block).
  uint32_t first_block_mpn0 = kInvalidPage;

  // Write out kPagesPerBlock + 1 pages to alternating map pages so that we consume 2 map blocks.
  for (uint32_t page = 0; page < (kPagesPerBlock + 1); ++page) {
    // Alternate writing to page 0/kMappingsPerMpn, which will update MPNs 0/1 respectively.
    ASSERT_EQ(ZX_OK, volume->Write((page % 2) ? kMappingsPerMpn : 0, 1, buf));
    ASSERT_EQ(ZX_OK, volume->Flush());

    if (page == 0) {
      // Ensure mpns[0] is now valid, and store the block it's in.
      ASSERT_NE(ftl->mpns[0], kInvalidPage);
      first_block_mpn0 = ftl->mpns[0] / kPagesPerBlock;
    }
  }

  uint32_t phys_page0_old = kInvalidPage;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, 0, &phys_page0_old));
  ASSERT_NE(phys_page0_old, kInvalidPage);

  uint32_t phys_page1_old = kInvalidPage;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, kMappingsPerMpn, &phys_page1_old));
  ASSERT_NE(phys_page1_old, kInvalidPage);

  // Now we simulate the 2nd page in the first map block going bad.
  // This should result in that physical block being erased when we re-mount.
  driver_unowned->SetFailEcc((first_block_mpn0 * kPagesPerBlock) + 1, true);

  // Remount with the corruption.
  volume->ReAttach();

  ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());

  // We should expect first_block_mpn0 to now be erased.
  ASSERT_TRUE(IS_FREE(ftl->bdata[first_block_mpn0]));

  // At this point we've effectively lost all mappings in MPN1 but still have MPN0.
  ASSERT_NE(ftl->mpns[0], kInvalidPage);
  uint32_t phys_page0_new = kInvalidPage;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, 0, &phys_page0_new));
  ASSERT_EQ(phys_page0_new, phys_page0_old);

  ASSERT_EQ(ftl->mpns[1], kInvalidPage);
  uint32_t phys_page1_new = kInvalidPage;
  ASSERT_EQ(0, FtlnMapGetPpn(ftl, kMappingsPerMpn, &phys_page1_new));
  ASSERT_EQ(phys_page1_new, kInvalidPage);
}

// Purposely generates a high garbage level by interleaving which vpages get written, stops after
// partial volume and map blocks to waste that space as well.
void FillWithGarbage(FtlShell* ftl, uint32_t num_blocks) {
  uint8_t buf[kPageSize];
  memcpy(buf, "abc123", 6);
  ftl::Volume* volume = ftl->volume();
  // First write every page in order.
  for (uint32_t i = 0; i < ftl->num_pages(); ++i) {
    ASSERT_EQ(ZX_OK, volume->Write(i, 1, buf));
  }

  // Now exhaust any breathing room by replacing 1 page from each volume block.
  uint32_t page = 0;
  for (uint32_t num_writes = ftl->num_pages(); num_writes < kPagesPerBlock * num_blocks;
       ++num_writes) {
    ASSERT_EQ(ZX_OK, volume->Write(page, 1, buf));
    page += kPagesPerBlock;
    if (page >= ftl->num_pages()) {
      // If we go off the end, we'll now replace the second page of each physical block.
      page = (page % kPagesPerBlock) + 1;
    }
  }

  // If we happen to end on a complete volume block, add another write.
  if (std::max(kPagesPerBlock * num_blocks, ftl->num_pages()) % kPagesPerBlock == 0) {
    ASSERT_EQ(ZX_OK, volume->Write(page, 1, buf));
  }
}

// Ensure we can remount after filling with garbage.
TEST(FtlTest, HighGarbageLevelRemount) {
  FtlShell ftl_shell;
  auto driver = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  ASSERT_EQ(nullptr, driver->Init());
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver)));
  ASSERT_NO_FATAL_FAILURE(FillWithGarbage(&ftl_shell, kDefaultOptions.num_blocks));

  // Flush and remount. Not enough map pages to fill the map block.
  ftl::Volume* volume = ftl_shell.volume();
  ASSERT_EQ(ZX_OK, volume->Flush());
  ASSERT_EQ(nullptr, volume->ReAttach());
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
  // The FTL maintains this minimum number of free blocks. During mount it will need to grab 2 of
  // them, one for new map pages, one for new volume pages, which means that we'll need to recycle
  // and reclaim blocks, first of which it will try to recover are the half-finished map block and
  // volume block. This should be true if we've generated enough "garbage" in the volume.
  EXPECT_LE(ftl->num_free_blks, static_cast<uint32_t>(FTLN_MIN_FREE_BLKS));

  // Ensure that we can perform a read.
  uint8_t buf[kPageSize];
  ASSERT_EQ(ZX_OK, volume->Read(1, 1, buf));
}

// It is a critical invariant that the erase list is the last thing written at shutdown and then
// erased before any other mutating operations at mount. We fill the volume with garbage first to
// trigger block recycles on mount, which should never happen before the erase list is removed.
TEST(FtlTest, EraseListLastAndFirst) {
  std::mutex lock;
  uint32_t last_write_page = 0;
  uint32_t first_mutation_page = 0;
  NdmRamDriver::Op first_mutation_type = NdmRamDriver::Op::Read;
  bool first_mutation_done = false;

  FtlShell ftl_shell;
  auto driver = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  driver->set_operation_callback([&](NdmRamDriver::Op op, uint32_t page) {
    std::lock_guard l(lock);
    // Exclude the 2 blocks used for ndm metadata.
    if (page / kPagesPerBlock > kDefaultOptions.num_blocks - 3) {
      return 0;
    }
    if ((op == NdmRamDriver::Op::Write || op == NdmRamDriver::Op::Erase) && !first_mutation_done) {
      first_mutation_done = true;
      first_mutation_type = op;
      first_mutation_page = page;
    }
    if (op == NdmRamDriver::Op::Write) {
      last_write_page = page;
    }
    return 0;
  });
  ASSERT_EQ(nullptr, driver->Init());
  NdmRamDriver* unowned_driver = driver.get();
  ASSERT_TRUE(ftl_shell.InitWithDriver(std::move(driver)));

  ASSERT_NO_FATAL_FAILURE(FillWithGarbage(&ftl_shell, kDefaultOptions.num_blocks));
  ftl::Volume* volume = ftl_shell.volume();
  ASSERT_EQ(ZX_OK, volume->Flush());

  // Recycle the map page which eagerly erases. This way there is something for the erase list to
  // contain. Do it twice since we need at least 2 erased blocks to write out an erase list.
  FTLN ftl = reinterpret_cast<FTLN>(
      reinterpret_cast<ftl::VolumeImpl*>(volume)->GetInternalVolumeForTest());
  uint32_t meta_page = ftl->num_map_pgs - 1;
  uint32_t phys_map_page = ftl->mpns[0];
  ASSERT_NE(0xFFFFFFFFu, phys_map_page);
  ASSERT_EQ(0, FtlnRecycleMapBlk(ftl, phys_map_page / kPagesPerBlock));
  ASSERT_NE(phys_map_page, ftl->mpns[0]);
  phys_map_page = ftl->mpns[0];
  ASSERT_NE(0xFFFFFFFFu, phys_map_page);
  ASSERT_EQ(0, FtlnRecycleMapBlk(ftl, phys_map_page / kPagesPerBlock));
  ASSERT_NE(phys_map_page, ftl->mpns[0]);

  // Erase list gets written during unmount.
  volume->Unmount();

  uint32_t last_write_unmount = 0;
  {
    // Save the last mutation from unmount, which should be a write.
    std::lock_guard l(lock);
    last_write_unmount = last_write_page;

    // Reset the first_mutation bit so that it will recapture on mount.
    first_mutation_done = false;
  }

  // Get the spare for it.
  uint8_t spare_buf[kSpareSize];
  ASSERT_EQ(ZX_OK, unowned_driver->NandRead(last_write_unmount, 1, nullptr, spare_buf));
  // It is a meta-page, which can only be an erase or continuation of an erase page.
  ASSERT_NE(static_cast<uint32_t>(-1), GET_SA_BC(spare_buf));
  ASSERT_EQ(meta_page, GET_SA_VPN(spare_buf));

  // Remount. Verify that first mutation was the deletion.
  ASSERT_EQ(nullptr, volume->ReAttach());
  uint8_t page_buf[kPageSize];
  ASSERT_EQ(ZX_OK, volume->Write(0, 1, page_buf));
  {
    std::lock_guard l(lock);
    // First mutation should be to the block containing the erase list.
    ASSERT_TRUE(first_mutation_done);
    ASSERT_EQ(NdmRamDriver::Op::Erase, first_mutation_type);
    ASSERT_EQ(last_write_unmount / kPagesPerBlock, first_mutation_page / kPagesPerBlock);
  }
}

}  // namespace
