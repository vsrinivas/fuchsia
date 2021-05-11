// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

class FwTest : public SingleApTest {
 public:
  FwTest() {}
  ~FwTest() {}
};

TEST_F(FwTest, TestPageInit) {
  auto mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());

  // to 44KB ((8+3)*4KB) so that 2 blocks (8 pages + 3 pages) will be created in the destination
  // DRAM.
  //
  //   Block ID  #pages
  //   #1        8 (NUM_OF_PAGE_PER_GROUP)
  //   #2        3 (NUM_OF_PAGES_IN_LAST_BLK)
  //
  // Note that we skip the blk#0 above, which is always 4KB (FW_PAGING_SIZE). We will verify that
  // after page initializaion is done below.
  //
  const size_t NUM_OF_PAGE_BLK = 2;
  const size_t NUM_OF_PAGES_IN_LAST_BLK = 3;  // the page number in the 'CPU2 paging image' block.
  const size_t PAGING_MEM_SIZE =
      FW_PAGING_SIZE * (NUM_OF_PAGE_PER_GROUP * (NUM_OF_PAGE_BLK - 1) + NUM_OF_PAGES_IN_LAST_BLK);
  uint8_t arbitrary_data[PAGING_MEM_SIZE] = {};
  for (size_t i = 0; i < sizeof(arbitrary_data); i++) {
    arbitrary_data[i] = i;  // fill with arbitrary data for testing.
  }

  // About the sections, see iwl_fill_paging_mem().
  struct fw_desc sec[] = {
      {
          // CPU1 section
      },
      {
          // CPU1 section
      },
      {
          // CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between CPU1 to CPU2
          .offset = CPU1_CPU2_SEPARATOR_SECTION,
      },
      {
          // CPU2 sections (not paged)
      },
      {
          // PAGING_SEPARATOR_SECTION delimiter - separate between CPU2 non paged to CPU2 paging
          // sec
          .offset = PAGING_SEPARATOR_SECTION,
      },
      {
          // CPU2 paging CSS  (4KB)
          .data = arbitrary_data,
          .len = FW_PAGING_SIZE,
      },
      {
          // CPU2 paging image: (8 + 3) * 4KB
          .data = arbitrary_data,
          .len = PAGING_MEM_SIZE,
      },
  };
  struct iwl_fw fw = {
      .img =
          {
              // IWL_UCODE_REGULAR
              {},

              // IWL_UCODE_INIT
              {
                  .sec = sec,
                  .num_sec = ARRAY_SIZE(sec),
                  .paging_mem_size = PAGING_MEM_SIZE,
              },

              // IWL_UCODE_WOWLAN
              {},

              // IWL_UCODE_REGULAR_USNIFFER
              {},
          },
  };
  struct iwl_fw_runtime fwrt = {
      .trans = sim_trans_.iwl_trans(),
      .fw = &fw,
  };

  mtx_lock(&mvm->mutex);
  zx_status_t ret = iwl_init_paging(&fwrt, IWL_UCODE_INIT);
  mtx_unlock(&mvm->mutex);
  EXPECT_EQ(ret, ZX_OK);
  EXPECT_EQ(fwrt.num_of_paging_blk, NUM_OF_PAGE_BLK);
  EXPECT_EQ(fwrt.num_of_pages_in_last_blk, NUM_OF_PAGES_IN_LAST_BLK);
  // CPU2 paging CSS
  EXPECT_EQ(io_buffer_size(&fwrt.fw_paging_db[0].io_buf, 0), FW_PAGING_SIZE);
  // CPU2 paging image: blk#0
  EXPECT_EQ(io_buffer_size(&fwrt.fw_paging_db[1].io_buf, 0), PAGING_BLOCK_SIZE);
  EXPECT_EQ(
      memcmp(io_buffer_virt(&fwrt.fw_paging_db[1].io_buf), &arbitrary_data[0], PAGING_BLOCK_SIZE),
      0);
  // CPU2 paging image: blk#1.
  // Allocated PAGING_BLOCK_SIZE, but only NUM_OF_PAGES_IN_LAST_BLK pages are copied.
  EXPECT_EQ(io_buffer_size(&fwrt.fw_paging_db[2].io_buf, 0), PAGING_BLOCK_SIZE);
  EXPECT_EQ(memcmp(io_buffer_virt(&fwrt.fw_paging_db[2].io_buf), &arbitrary_data[PAGING_BLOCK_SIZE],
                   FW_PAGING_SIZE * NUM_OF_PAGES_IN_LAST_BLK),
            0);
}

}  // namespace
}  // namespace wlan::testing
