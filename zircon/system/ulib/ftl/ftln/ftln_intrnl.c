// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftlnp.h"

// Configuration
#ifndef FTLN_DEBUG_RECYCLES
#define FTLN_DEBUG_RECYCLES FALSE
#endif
#define SHOW_LAG_HIST FALSE

// Type Definitions
typedef struct {
  ui32 vpn0;
  ui32 ppn0;
  ui32 cnt;
  const ui8* buf;
} StagedWr;

// Function Prototypes
#ifdef FTL_RESUME_STRESS
void FtlNumFree(uint num_free_blks);
#endif
#if FTLN_DEBUG_RECYCLES
static int recycle_possible(CFTLN ftl, ui32 b);
static ui32 block_selector(FTLN ftl, ui32 b);

// Local Function Definitions

//    show_blk: Show the state of a block using its blocks[] entry
//
//      Inputs: ftl = pointer to FTL control block
//              b = block number
//
static int show_blk(FTLN ftl, ui32 b) {
  int n;

  n = printf("b%03d ", b);
  if (IS_FREE(ftl->bdata[b])) {
    if (ftl->bdata[b] & ERASED_BLK_FLAG)
      n += printf("erased");
    else
      n += printf("free  ");
    n += printf(" wl=%03d ", ftl->blk_wc_lag[b]);
    return n;
  } else if (IS_MAP_BLK(ftl->bdata[b])) {
    putchar('m');
    ++n;
  } else {  // Volume block.
    putchar('v');
    ++n;
  }
  n += printf(" u=%-2d", NUM_USED(ftl->bdata[b]));
  n += printf(" wl=%03d ", ftl->blk_wc_lag[b]);
  if (!recycle_possible(ftl, b))
    n += printf("np");
  else
    n += printf("s=%d", block_selector(ftl, b));
  if (ftl->free_vpn / ftl->pgs_per_blk == b)
    n += printf(" FV");
  else if (ftl->free_mpn / ftl->pgs_per_blk == b)
    n += printf(" FM");
  return n;
}

//   show_blks: List the number free and the state of each block
//
//       Input: ftl = pointer to FTL control block
//
static void show_blks(FTLN ftl) {
  int n;
  uint l, q = (ftl->num_blks + 3) / 4;

  printf("num_free=%d", ftl->num_free_blks);
  for (l = 0; l < q; ++l) {
    putchar('\n');
    n = show_blk(ftl, l);
    if (l + q <= ftl->num_blks) {
      printf("%*s", 31 - n, " ");
      n = show_blk(ftl, l + q);
    }
    if (l + 2 * q <= ftl->num_blks) {
      printf("%*s", 31 - n, " ");
      n = show_blk(ftl, l + 2 * q);
    }
    if (l + 3 * q <= ftl->num_blks) {
      printf("%*s", 31 - n, " ");
      n = show_blk(ftl, l + 3 * q);
    }
  }
}

// check_lag_sum: Verify the wear lag sum and max lag calculations.
//
//       Input: ftl = pointer to FTL control block
//
static void check_lag_sum(FTLN ftl) {
  ui32 b, wc_lag, wc_lag_sum = 0, wc_lag_max = 0;

  for (b = 0; b < ftl->num_blks; ++b) {
    wc_lag = ftl->blk_wc_lag[b];
    if (wc_lag_max < wc_lag)
      wc_lag_max = wc_lag;
    wc_lag_sum += wc_lag;
  }
  PfAssert(ftl->wear_data.cur_max_lag == wc_lag_max);
  PfAssert(ftl->wc_lag_sum == wc_lag_sum);
  PfAssert(ftl->wear_data.avg_wc_lag == wc_lag_sum / ftl->num_blks);
}
#endif  // FTLN_DEBUG_RECYCLES

#if SHOW_LAG_HIST
// show_lag_hist: Print running histogram of block wear lag.
//
//       Input: ftl = pointer to FTL control block
//
static void show_lag_hist(FTLN ftl) {
  uint i, j = ftl->wear_data.cur_max_lag / 2;
  uint avg_lag_scaled = ftl->wear_data.avg_wc_lag / 2;

  for (i = 0; i < j; ++i) {
    if (i <= avg_lag_scaled)
      putchar('+');
    else
      putchar('-');
  }
  printf(" %u\n", ftl->wear_data.cur_max_lag);
}
#endif

// next_free_vpg: Get next free volume page
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: page number if successful, else (ui32)-1 if error
//
static ui32 next_free_vpg(FTLN ftl) {
  ui32 pn;

  // If needed, allocate new volume block if needed.
  if (ftl->free_vpn == (ui32)-1) {
    ui32 b;

    if (ftl->num_free_blks > FTL_FREE_THRESHOLD_FOR_LOW_WEAR_ALLOCATION) {
      // Find free block with the lowest wear count.
      b = FtlnLoWcFreeBlk(ftl);
    } else {
      // Find free block with highest wear count.
      b = FtlnHiWcFreeBlk(ftl);
    }
    // Error if none are free.
    if (b == (ui32)-1)
      return b;

    // If the block is unerased, erase it now. Return -1 if error.
    if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
      if (FtlnEraseBlk(ftl, b))
        return (ui32)-1;

    // Decrement free block count.
    PfAssert(ftl->num_free_blks);
    --ftl->num_free_blks;
#ifdef FTL_RESUME_STRESS
    FtlNumFree(ftl->num_free_blks);
#endif

    // Set free volume page pointer to first page in block.
    ftl->free_vpn = b * ftl->pgs_per_blk;

    // Clear block's free/erased flags and read count.
    ftl->bdata[b] = 0;  // clr free flag, used/read pages cnts
  }

  // Allocate free volume page. If end of block, invalidate free ptr.
  pn = ftl->free_vpn++;
  if (ftl->free_vpn % ftl->pgs_per_blk == 0)
    ftl->free_vpn = (ui32)-1;

  // Return allocate page number.
  return pn;
}

// next_free_mpg: Get next free map page
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: page number if successful, else (ui32)-1 if error
//
static ui32 next_free_mpg(FTLN ftl) {
  ui32 pn;

  // If needed, allocate new map block.
  if (ftl->free_mpn == (ui32)-1) {
    ui32 b;

    // Find free block with lowest wear count. Error if none are free.
    b = FtlnLoWcFreeBlk(ftl);
    if (b == (ui32)-1)
      return b;

    // If the block is unerased, erase it now. Return -1 if error.
    if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
      if (FtlnEraseBlk(ftl, b))
        return (ui32)-1;

    // Decrement free block count.
    PfAssert(ftl->num_free_blks);
    --ftl->num_free_blks;
#ifdef FTL_RESUME_STRESS
    FtlNumFree(ftl->num_free_blks);
#endif

    // Set free MPN pointer to first page in block and inc block count.
    ftl->free_mpn = b * ftl->pgs_per_blk;
    ++ftl->high_bc;

    // Clear free block flag and read count, set map block flag.
    SET_MAP_BLK(ftl->bdata[b]);  // clr free flag & wear/read count
  }

  // Use first page on free map page list.
  pn = ftl->free_mpn;

  // Move to next writable page. For MLC flash, that is page whose pair
  // has a higher offset. Invalidate index if end of block reached.
#if INC_FTL_NDM_SLC
  if (++ftl->free_mpn % ftl->pgs_per_blk == 0)
    ftl->free_mpn = (ui32)-1;
#else
  for (;;) {
    ui32 pg_offset = ++ftl->free_mpn % ftl->pgs_per_blk;

    if (pg_offset == 0) {
      ftl->free_mpn = (ui32)-1;
      break;
    }
    if (ftl->pair_offset(pg_offset, ftl->ndm) >= pg_offset)
      break;
  }

#if FS_ASSERT
  // For MLC devices, sanity check that this is a safe write.
  {
    ui32 pg_offset = pn % ftl->pgs_per_blk;

    PfAssert(ftl->pair_offset(pg_offset, ftl->ndm) >= pg_offset);
  }
#endif
#endif

  // Return allocated page number.
  return pn;
}

// wr_vol_page: Write a volume page to flash
//
//      Inputs: ftl = pointer to FTL control block
//              vpn = virtual page number
//              buf = pointer to page data buffer or NULL
//              old_pn = old location for page, if any
//
//     Returns: 0 on success, -1 on error
//
static int wr_vol_page(FTLN ftl, ui32 vpn, void* buf, ui32 old_ppn) {
  ui32 ppn, b, wc;
  int rc;

#if INC_ELIST
  // If list of erased blocks/wear counts exists, erase it now.
  if (ftl->elist_blk != (ui32)-1)
    if (FtlnEraseBlk(ftl, ftl->elist_blk)) {
      ftl->logger.error("Failed to erase block list at block %u.", ftl->elist_blk);
      return -1;
    }
#endif

  // Allocate next free volume page. Return -1 if error.
  ppn = next_free_vpg(ftl);
  if (ppn == (ui32)-1) {
    ftl->logger.error("Failed to allocate a volume page.");
    return -1;
  }

  // Calculate the block's erase wear count.
  b = ppn / ftl->pgs_per_blk;
  wc = ftl->high_wc - ftl->blk_wc_lag[b];
  PfAssert((int)wc > 0);

  // Initialize spare area, including VPN and block wear count.
  memset(ftl->spare_buf, 0xFF, ftl->eb_size);
  SET_SA_VPN(vpn, ftl->spare_buf);
  SET_SA_WC(wc, ftl->spare_buf);

  // If page data in buffer, write it. Returns 0 or -2.
  if (buf) {
    ++ftl->stats.write_page;
    rc = ndmWritePage(ftl->start_pn + ppn, buf, ftl->spare_buf, ftl->ndm);

    // Else invoke page transfer routine. Returns 0, -2, or 1.
  } else {
    ++ftl->stats.transfer_page;
    rc = ndmTransferPage(ftl->start_pn + old_ppn, ftl->start_pn + ppn, ftl->main_buf,
                         ftl->spare_buf, ftl->ndm);
  }

  // Return -1 for any error. Any write error is fatal.
  if (rc) {
    ftl->logger.error("Failed to write volume page %u at %u.", vpn, ftl->start_pn + ppn);
    return FtlnFatErr(ftl);
  }

  // Increment block's used pages count.
  PfAssert(!IS_FREE(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b]));
  INC_USED(ftl->bdata[b]);

  // If page has an older copy, decrement used count on old block.
  if (old_ppn != (ui32)-1)
    FtlnDecUsed(ftl, old_ppn, vpn);

  // Update mapping for this virtual page. Return status.
  return FtlnMapSetPpn(ftl, vpn, ppn);
}

// free_vol_list_pgs: Return number of free pages on free_vpn block
//
//       Input: ftl = pointer to FTL control block
//
static int free_vol_list_pgs(CFTLN ftl) {
  // If free_vpn has invalid page number, no list of free vol pages.
  if (ftl->free_vpn == (ui32)-1)
    return 0;

  // Use the page offset to get the number of free pages left on block.
  return ftl->pgs_per_blk - ftl->free_vpn % ftl->pgs_per_blk;
}

// free_map_list_pgs: Return number of free pages on free_mpn block
//
//       Input: ftl = pointer to FTL control block
//
static int free_map_list_pgs(CFTLN ftl) {
  int free_mpgs;

  // If free_mpn has invalid page number, no list of free map pages.
  if (ftl->free_mpn == (ui32)-1)
    return 0;

  // Use the page offset to get the number of free pages left on block.
  free_mpgs = ftl->pgs_per_blk - ftl->free_mpn % ftl->pgs_per_blk;

#if INC_FTL_NDM_MLC
  // We only use half the available pages on an MLC map block.
  free_mpgs /= 2;
#endif

  // Return the number of free pages on the free_mpn list.
  return free_mpgs;
}

// recycle_possible: Check if there are enough free blocks to recycle
//              a specified block
//
//      Inputs: ftl = pointer to FTL control block
//              b = potential recycle block to check
//
//     Returns: TRUE if enough free blocks, FALSE otherwise
//
static int recycle_possible(CFTLN ftl, ui32 b) {
  ui32 used, free_mpgs = 0, needed_free;

  // Determine how many used pages the prospective recycle block has.
  used = NUM_USED(ftl->bdata[b]);

  // If block has no used pages and it's a map block or no cached map
  // pages are dirty, no writes are needed and so recycle is possible.
  if ((used == 0) && (IS_MAP_BLK(ftl->bdata[b]) || (ftl->map_cache->num_dirty == 0)))
    return TRUE;

  // If free map page list is empty or on prospective recycle block,
  // need a new free map block, but get a bunch of free map pages.
  if ((ftl->free_mpn == (ui32)-1) || (ftl->free_mpn / ftl->pgs_per_blk == b)) {
    needed_free = 1;
    free_mpgs = ftl->pgs_per_blk;
#if INC_FTL_NDM_MLC
    // only use half of MLC map block pages
    free_mpgs /= 2;
#endif

    // Else get number of free map pages. If map block, need free block
    // if free map page count is less than this block's used page count.
  } else {
    free_mpgs = free_map_list_pgs(ftl);
    if (IS_MAP_BLK(ftl->bdata[b]))
      needed_free = free_mpgs < used;
    else
      needed_free = 0;
  }

  // If volume block, free blocks may be needed for both volume page
  // transfer and the post-recycle map cache flush.
  if (!IS_MAP_BLK(ftl->bdata[b])) {
    ui32 avail_blk_pgs, map_pgs;

    // If free volume page list is empty or on the prospective block,
    // need new free volume block.
    if ((ftl->free_vpn == (ui32)-1) || (ftl->free_vpn / ftl->pgs_per_blk == b))
      ++needed_free;

    // Else if the number of free volume pages is less than the number
    // of used pages on prospective block, need another volume block.
    else if ((ui32)free_vol_list_pgs(ftl) < used)
      ++needed_free;

    // Assume each volume page transfer updates a separate map page
    // (worst case). Add the number of dirty cached map pages. If this
    // exceeds the number of free map pages, add needed map blocks.
    map_pgs = used + ftl->map_cache->num_dirty;
    if (map_pgs > free_mpgs) {
      avail_blk_pgs = ftl->pgs_per_blk;
#if INC_FTL_NDM_MLC
      // only use half of MLC map block pages
      avail_blk_pgs /= 2;
#endif
      needed_free += (map_pgs - free_mpgs + avail_blk_pgs - 1) / avail_blk_pgs;
    }
  }

  // For recovery from worst-case powerfail recovery interruption,
  // recycles must leave one free block for the resume process.
  ++needed_free;

  // Recycles are possible if there are enough free blocks.
  return ftl->num_free_blks >= needed_free;
}

// block_selector: Compute next recycle block selector for a block: a
//              combination of its dirty page count, erase wear count,
//              and read wear count
//
//      Inputs: ftl = pointer to FTL control block
//              b = block to compute selector for
//              should_boost_low_wear = prioritise blocks with low wear
//
//     Returns: Selector used to determine whether block is recycled
//
static ui32 block_selector(FTLN ftl, ui32 b, int should_boost_low_wear) {
  ui32 pages_gained, priority;

  // Get number of free pages gained. Only half of MLC map block pages
  // are available.
  pages_gained = ftl->pgs_per_blk;
#if INC_FTL_NDM_MLC
  if (ftl->type == NDM_MLC && IS_MAP_BLK(ftl->bdata[b]))
    pages_gained /= 2;
#endif
  pages_gained -= NUM_USED(ftl->bdata[b]);

  priority = pages_gained * 256 + ftl->blk_wc_lag[b];

  // Boost a block's priority if requested and considered low wear.
  if (should_boost_low_wear &&
      (ftl->blk_wc_lag[b] + FTL_LOW_WEAR_BOOST_LAG > ftl->wear_data.cur_max_lag)) {
    priority += 0x100000;
  }

  // If block's read count is too high, there is danger of losing its
  // data, so add a priority boost that overwhelms the other facters.
  if (GET_RC(ftl->bdata[b]) >= ftl->max_rc)
    priority += 0x200000;

  // Return priority value (higher value is better to recycle).
  return priority;
}

// next_recycle_blk: Choose next block (volume or map) to recycle
//
//       Input: ftl = pointer to FTL control block
//              should_boost_low_wear = prioritise blocks with low wear
//
//     Returns: Chosen recycle block, (ui32)-1 on error
//
static ui32 next_recycle_blk(FTLN ftl, int should_boost_low_wear) {
  ui32 b, rec_b, selector, best_selector = 0;

  // Initially set flag as if no block is at the max read-count limit.
  ftl->max_rc_blk = (ui32)-1;

  // Scan blocks to select block to recycle.
  for (b = 0, rec_b = (ui32)-1; b < ftl->num_blks; ++b) {
    // Skip free blocks.
    if (IS_FREE(ftl->bdata[b]))
      continue;

    // Check if block is at read-wear limit.
    if (GET_RC(ftl->bdata[b]) >= ftl->max_rc) {
      // If first block, save its number, else mark as 'some at limit'.
      if (ftl->max_rc_blk == (ui32)-1)
        ftl->max_rc_blk = b;
      else
        ftl->max_rc_blk = (ui32)-2;
    }

    // Else not at read-wear limit, skip blocks holding a free list.
    else if (ftl->free_vpn / ftl->pgs_per_blk == b || ftl->free_mpn / ftl->pgs_per_blk == b)
      continue;

    // If recycle not possible on this block, skip it.
    if (recycle_possible(ftl, b) == FALSE)
      continue;

    // Compute block selector.
    selector = block_selector(ftl, b, should_boost_low_wear);

    // If no recycle block selected yet, or if the current block has a
    // higher selector value, remember it. Also, if the selector value
    // is the same, prefer volume blocks over map blocks to avoid map
    // blocks being recycled too often when the volume is full.
    if (rec_b == (ui32)-1 || best_selector < selector ||
        (best_selector == selector && !IS_MAP_BLK(ftl->bdata[b]) &&
         IS_MAP_BLK(ftl->bdata[rec_b]))) {
      rec_b = b;
      best_selector = selector;
    }
  }

  // If no recycle block found, try one of the partially written ones.
  if (rec_b == (ui32)-1) {
    // Check if block holding free volume page pointer can be used.
    if (ftl->free_vpn != (ui32)-1) {
      b = ftl->free_vpn / ftl->pgs_per_blk;
      if (recycle_possible(ftl, b)) {
        rec_b = b;
        best_selector = block_selector(ftl, b, should_boost_low_wear);
      }
    }

    // Check if free map page list block can be used and is better.
    if (ftl->free_mpn != (ui32)-1) {
      b = ftl->free_mpn / ftl->pgs_per_blk;
      selector = block_selector(ftl, b, should_boost_low_wear);
      if (recycle_possible(ftl, b) && selector > best_selector) {
        rec_b = b;
        best_selector = selector;
      }
    }
  }

  // If one of the partially written blocks has been selected,
  // invalidate the corresponding head of free space.
  if (rec_b != (ui32)-1) {
    if (ftl->free_mpn / ftl->pgs_per_blk == rec_b)
      ftl->free_mpn = (ui32)-1;
    else if (ftl->free_vpn / ftl->pgs_per_blk == rec_b)
      ftl->free_vpn = (ui32)-1;
  }

#if FTLN_DEBUG
  if (rec_b == (ui32)-1) {
    puts("FTL NDM failed to choose next recycle block!");
    FtlnBlkStats(ftl);
  }
#endif

#if FTLN_DEBUG_RECYCLES
  if (ftl->flags & FTLN_VERBOSE)
    printf("\nrec_b=%d\n", rec_b);
#endif

  // Return block chosen for next recycle.
  return rec_b;
}

// recycle_vblk: Recycle one volume block
//
//      Inputs: ftl = pointer to FTL control block
//              recycle_b = block to be recycled
//
//     Returns: 0 on success, -1 on error
//
static int recycle_vblk(FTLN ftl, ui32 recycle_b) {
  ui32 pn, past_end;

#if FTLN_DEBUG > 1
  if (ftl->flags & FTLN_VERBOSE)
    printf("recycle_vblk: block# %u\n", recycle_b);
#endif

  // Transfer all used pages from recycle block to free block.
  pn = recycle_b * ftl->pgs_per_blk;
  past_end = pn + ftl->pgs_per_blk;
  for (; NUM_USED(ftl->bdata[recycle_b]); ++pn) {
    int rc;
    ui32 vpn, pn2;

    // Error if looped over block and not enough valid used pages.
    if (pn >= past_end)
      return FtlnFatErr(ftl);

    // Read page's spare area.
    ++ftl->stats.read_spare;
    rc = ndmReadSpare(ftl->start_pn + pn, ftl->spare_buf, ftl->ndm);

    // Return -1 if fatal error, skip page if ECC error on spare read.
    if (rc) {
      if (rc == -2) {
        ftl->logger.error("Failed to read spare area from block %u.", recycle_b);
        return FtlnFatErr(ftl);
      } else
        continue;
    }

    // Get virtual page number from spare. Skip page if unmapped.
    vpn = GET_SA_VPN(ftl->spare_buf);
    if (vpn > ftl->num_vpages)
      continue;

    // Retrieve physical page number for VPN. Return -1 if error.
    if (FtlnMapGetPpn(ftl, vpn, &pn2) < 0)
      return -1;

    // Skip page copy if physical mapping is outdated.
    if (pn2 != pn)
      continue;

    // Write page to new flash block. Return -1 if error.
    if (wr_vol_page(ftl, vpn, NULL, pn)) {
      ftl->logger.error("Failed to transfer page %u to physical page %u.", vpn, pn);
      return -1;
    }
  }

  // Save MPGs modified by volume page transfers. Return -1 if error.
  if (ftlmcFlushMap(ftl->map_cache)) {
    ftl->logger.error("Failed to flush map cache.");
    return -1;
  }

#if INC_FTL_NDM_MLC
  // For MLC devices, advance free_vpn pointer so next volume page
  // write can't corrupt previously written valid page.
  FtlnMlcSafeFreeVpn(ftl);
#endif

  // Mark recycle block as free. Increment free block count.
  ftl->bdata[recycle_b] = FREE_BLK_FLAG;
  ++ftl->num_free_blks;

  // If this is last block at RC limit, flag that none are at limit.
  if (ftl->max_rc_blk == recycle_b)
    ftl->max_rc_blk = (ui32)-1;

  // Return success.
  return 0;
}

//     recycle: Perform a block recycle
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 on success, -1 on error
//
static int recycle(FTLN ftl, int should_boost_low_wear) {
  ui32 rec_b;

  // Confirm no physical page number changes in critical sections.
  PfAssert(ftl->assert_no_recycle == FALSE);

#if FTLN_DEBUG_RECYCLES
  // If enabled, list the number of free blocks and the state of each.
  if (ftl->flags & FTLN_VERBOSE) {
    printf("num_free_blks=%d\n", ftl->num_free_blks);
    show_blks(ftl);
  }

  // Verify the wear lag sum and max lag calculations.
  check_lag_sum(ftl);
#endif

  // Select next block to recycle. Return error if unable.
  rec_b = next_recycle_blk(ftl, should_boost_low_wear);
  if (rec_b == (ui32)-1) {
    PfAssert(FALSE);
    return FsError2(FTL_NO_RECYCLE_BLK, ENOSPC);
  }

  // Increment recycle count.
  ++ftl->wear_data.recycle_cnt;

  // Recycle the selected block. Return status.
  if (IS_MAP_BLK(ftl->bdata[rec_b]))
    return FtlnRecycleMapBlk(ftl, rec_b);
  else
    return recycle_vblk(ftl, rec_b);
}

// flush_pending_writes: Write any pending consecutive writes to flash
//
//      Inputs: ftl = pointer to FTL control block
//              staged = pointer to structure holding VPN, PPN, count,
//                       and buffer pointer for staged writes
//
//     Returns: 0 on success, -1 on failure
//
static int flush_pending_writes(FTLN ftl, StagedWr* staged) {
  ui32 end, b = staged->ppn0 / ftl->pgs_per_blk;

#if INC_ELIST
  // If list of erased blocks/wear counts exists, erase it now.
  if (ftl->elist_blk != (ui32)-1)
    if (FtlnEraseBlk(ftl, ftl->elist_blk))
      return -1;
#endif

  // Issue driver multi-page write request. Return -1 if error.
  ftl->stats.write_page += staged->cnt;
  if (ndmWritePages(ftl->start_pn + staged->ppn0, staged->cnt, staged->buf, ftl->spare_buf,
                    ftl->ndm)) {
    ftl->logger.error("Failed to stage writes.");
    return FtlnFatErr(ftl);
  }

  // Adjust data buffer pointer.
  staged->buf += staged->cnt * ftl->page_size;

  // Loop over all written pages to update mappings.
  end = staged->ppn0 + staged->cnt;
  for (; staged->ppn0 < end; ++staged->ppn0, ++staged->vpn0) {
    ui32 cur_ppn;

    // Retrieve current page mapping. Return -1 if error.
    if (FtlnMapGetPpn(ftl, staged->vpn0, &cur_ppn) < 0)
      return -1;

    // If mapping exists, decrement number of used in old block.
    if (cur_ppn != (ui32)-1)
      FtlnDecUsed(ftl, cur_ppn, staged->vpn0);

    // Increment number of used in new block.
    PfAssert(!IS_FREE(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b]));
    INC_USED(ftl->bdata[b]);

    // Update virtual page mapping. Return -1 if error.
    if (FtlnMapSetPpn(ftl, staged->vpn0, staged->ppn0))
      return -1;
    PfAssert(ftl->num_free_blks >= FTLN_MIN_FREE_BLKS);
  }

  // Clear pending count and return success.
  staged->cnt = 0;
  return 0;
}

// Global Function Definitions

// FtlnWrPages: Write count number of volume pages to flash
//
//      Inputs: buf = pointer to buffer holding write data
//              vpn = first page to write
//              count = number of pages to write
//              vol = pointer to FTL control block
//
//     Returns: 0 on success, -1 on error
//
int FtlnWrPages(const void* buf, ui32 vpn, int count, void* vol) {
  FTLN ftl = vol;
  StagedWr staged;
  int need_recycle;
  ui8* spare = ftl->spare_buf;
  uint wr_amp, fl_wr_cnt0, vol_writes = count;

  // Ensure request is within volume's range of provided pages.
  if (vpn + count > ftl->num_vpages)
    return FsError2(FTL_ASSERT, ENOSPC);

  // If no pages to write, return success.
  if (count == 0)
    return 0;

  // Set errno and return -1 if fatal I/O error occurred.
  if (ftl->flags & FTLN_FATAL_ERR)
    return FsError2(NDM_EIO, EIO);

  // Save flash page write count for ending FTL metric calculation.
  fl_wr_cnt0 = ftl->stats.write_page;

  // Initialize structures for staging deferred consecutive page writes.
  staged.buf = buf;
  staged.cnt = 0;

  // Check if recycles are needed for one page write.
  need_recycle = FtlnRecNeeded(ftl, 1);

  // Loop while there are whole pages to write.
  do {
    ui32 ppn, wc;

    // If needed, recycle blocks until at least one page is free.
    if (need_recycle)
      if (FtlnRecCheck(ftl, 1))
        return -1;

    // Allocate next free volume page. Return -1 if error.
    ppn = next_free_vpg(ftl);
    if (ppn == (ui32)-1)
      return -1;

    // If no pending writes, start new sequence. Else add to it.
    if (staged.cnt == 0) {
      staged.vpn0 = vpn;
      staged.ppn0 = ppn;
      staged.cnt = 1;
      spare = ftl->spare_buf;
    } else
      ++staged.cnt;

    // Copy page's VPN and block's BC/WC to the spare area.
    memset(spare, 0xFF, ftl->eb_size);
    SET_SA_VPN(vpn, spare);
    wc = ftl->high_wc - ftl->blk_wc_lag[ppn / ftl->pgs_per_blk];
    PfAssert((int)wc > 0);
    SET_SA_WC(wc, spare);
    spare += ftl->eb_size;

    // Check if writing one page more than staged would trigger a
    // recycle or if the physical page is last in its block.
    need_recycle = FtlnRecNeeded(ftl, staged.cnt + 1);
    if (need_recycle || (ftl->free_vpn == (ui32)-1)) {
      // Flush currently staged pages.
      if (flush_pending_writes(ftl, &staged))
        return -1;

      // Invoke recycles to prepare for the next page write.
      need_recycle = TRUE;
    }

    // Adjust volume page number and count.
    ++vpn;
  } while (--count > 0);

  // If there are any, flush pending writes.
  if (staged.cnt) {
    if (FtlnRecCheck(ftl, staged.cnt))
      return -1;
    if (flush_pending_writes(ftl, &staged))
      return -1;
  }

  // Update FTL vol page write count and write amplification metrics.
  ftl->vol_pg_writes += vol_writes;
  count = ftl->stats.write_page - fl_wr_cnt0;
  wr_amp = (10 * count) / vol_writes;
  wr_amp = (wr_amp + 5) / 10;
  if (ftl->wear_data.write_amp_max < wr_amp)
    ftl->wear_data.write_amp_max = wr_amp;

  // Return success.
  return 0;
}

// FtlnRecNeeded: Determine if dirty flash pages need to be reclaimed
//
//      Inputs: ftl = pointer to FTL control block
//              wr_cnt = the number and type of pending page writes,
//                       in addition to dirty map cache pages:
//                       < 0 -> one map page
//                       > 0 -> wr_cnt number of volume pages
//                       = 0 -> no additional (besides map cache)
//
int FtlnRecNeeded(CFTLN ftl, int wr_cnt) {
  int free_pgs, need, mblks_req, vblks_req;

  // Return TRUE if some block is at read count max.
  if (ftl->max_rc_blk != (ui32)-1)
    return TRUE;

  // Return TRUE if in powerfail recovery of an interrupted recycle.
  if (ftl->num_free_blks < FTLN_MIN_FREE_BLKS)
    return TRUE;

  // If free map list can hold all currently dirty map cache pages and
  // those potentially made dirty by the vol page write, no more map
  // blocks are needed. Otherwise convert needed map pages to blocks.
  free_pgs = free_map_list_pgs(ftl);
  if (wr_cnt < 0)
    need = 1;  // writing one map page
  else
    need = wr_cnt;  // each volume page can update one map page
  need += ftl->map_cache->num_dirty;
  if (free_pgs >= need) {
    mblks_req = 0;
  } else {
    ui32 avail_blk_pgs = ftl->pgs_per_blk;

#if INC_FTL_NDM_MLC
    avail_blk_pgs /= 2;
#endif
    mblks_req = (need - free_pgs + avail_blk_pgs - 1) / avail_blk_pgs;
  }

  // If free volume list can hold all requested write pages, no more
  // volume blocks are needed. Otherwise convert needed pages to blocks
  free_pgs = free_vol_list_pgs(ftl);
  if (wr_cnt <= free_pgs)
    vblks_req = 0;  // no new volume blocks needed
  else
    vblks_req = (wr_cnt - free_pgs + ftl->pgs_per_blk - 1) / ftl->pgs_per_blk;

  // Need recycle if number of required blocks is more than is free.
  return (ui32)(mblks_req + vblks_req + FTLN_MIN_FREE_BLKS) > ftl->num_free_blks;
}

// FtlnRecycleMapBlk: Recycle one map block
//
//      Inputs: ftl = pointer to FTL control block
//              recycle_b = block to be recycled
//
//     Returns: 0 on success, -1 on error
//
int FtlnRecycleMapBlk(FTLN ftl, ui32 recycle_b) {
  ui32 i, pn;

#if FTLN_DEBUG > 1
  if (ftl->flags & FTLN_VERBOSE)
    printf("FtlnRecycleMapBlk: block# %u\n", recycle_b);
#endif

  // Transfer all used pages from recycle block to free block.
  pn = recycle_b * ftl->pgs_per_blk;
  for (i = 0; NUM_USED(ftl->bdata[recycle_b]); ++pn, ++i) {
    int rc;
    ui32 mpn;
    void* buf;

    // Error if looped over block and not enough valid used pages.
    if (i >= ftl->pgs_per_blk)
      return FtlnFatErr(ftl);

#if INC_FTL_NDM_MLC
    // For MLC NAND, if not first page on block, skip pages whose pair
    // is at a lower offset, to not corrupt them by a failed write.
    if (i && (ftl->pair_offset(i, ftl->ndm) < i))
      continue;
#endif

    // Read page's spare area. Return -1 if fatal I/O error.
    ++ftl->stats.read_spare;
    rc = ndmReadSpare(ftl->start_pn + pn, ftl->spare_buf, ftl->ndm);
    if (rc == -2)
      return FtlnFatErr(ftl);

    // Skip page if ECC error on spare read.
    if (rc < 0)
      continue;

    // Get map page number from page's spare area.
    mpn = GET_SA_VPN(ftl->spare_buf);

    // Skip page if meta page or physical page mapping is outdated.
    if (mpn >= ftl->num_map_pgs - 1 || ftl->mpns[mpn] != pn)
      continue;

    // Get pointer to its data if page-to-be-written is in cache.
    buf = ftlmcInCache(ftl->map_cache, mpn);

    // Write page to new flash block. Return -1 if error.
    if (FtlnMapWr(ftl, mpn, buf))
      return -1;
  }

  // Erase recycled map block. Return -1 if error.
  if (FtlnEraseBlk(ftl, recycle_b))
    return -1;

  // If this is last block at RC limit, flag that none are at limit.
  if (ftl->max_rc_blk == recycle_b)
    ftl->max_rc_blk = (ui32)-1;

  // Return success.
  return 0;
}

//  FtlnMetaWr: Write FTL meta information page
//
//      Inputs: ftl = pointer to FTL control block
//              type = metapage type
//
//        Note: The caller should initialize all but the first 8 bytes
//              of 'main_buf' before calling this routine.
//
//     Returns: 0 on success, -1 on error
//
int FtlnMetaWr(FTLN ftl, ui32 type) {
  // Write metapage version number and type.
  WR32_LE(FTLN_META_VER1, &ftl->main_buf[FTLN_META_VER_LOC]);
  WR32_LE(type, &ftl->main_buf[FTLN_META_TYP_LOC]);

  // Issue meta page write.
  int result = FtlnMapWr(ftl, ftl->num_map_pgs - 1, ftl->main_buf);
  if (result < 0) {
    ftl->logger.error("FTL failed to write meta map page.");
  }
  return result;
}

// FtlnRecCheck: Prepare to write page(s) by reclaiming dirty blocks
//               in advance to (re)establish the reserved number of
//               free blocks
//
//      Inputs: ftl = pointer to FTL control block
//              wr_cnt = the number and type of page writes to prepare
//                       for, in addition to dirty map cache pages:
//                       < 0 -> one map page
//                       > 0 -> wr_cnt number of volume pages
//                       = 0 -> no additional (besides map cache)
//
//     Returns: 0 on success, -1 on error
//
int FtlnRecCheck(FTLN ftl, int wr_cnt) {
  // Set errno and return -1 if fatal I/O error occurred.
  if (ftl->flags & FTLN_FATAL_ERR)
    return FsError2(NDM_EIO, EIO);

  // Check if a recycle is needed.
  if (FtlnRecNeeded(ftl, wr_cnt)) {
    uint count;

    // Count number of times any recycle is done in FtlnRecCheck().
    ++ftl->recycle_needed;

#if FTLN_DEBUG > 1
    if (ftl->flags & FTLN_VERBOSE)
      printf(
          "\n0 rec begin: free vpn = %5u (%3u), free mpn = %5u (%3u)"
          " free blocks = %2u\n",
          ftl->free_vpn, free_vol_list_pgs(ftl), ftl->free_mpn, free_map_list_pgs(ftl),
          ftl->num_free_blks);
#endif

    // Loop until enough pages are free.
    for (count = 1;; ++count) {
      // Perform one recycle operation. Return -1 if error.
      if (recycle(ftl, /*should_boost_low_wear=*/count & 1) != 0)
        return -1;

#if SHOW_LAG_HIST
      // Print running histogram of block wear lag.
      show_lag_hist(ftl);
#endif

      // Record the highest number of consecutive recycles.
      if (ftl->wear_data.max_consec_rec < count) {
        ftl->wear_data.max_consec_rec = count;
#if FTLN_DEBUG > 2
        printf("max_consec_rec=%u, avg_wc_lag=%u\n", ftl->wear_data.max_consec_rec,
               ftl->wear_data.avg_wc_lag);
#endif
      }

#if FTLN_DEBUG > 1
      if (ftl->flags & FTLN_VERBOSE)
        printf(
            "%u rec begin: free vpn = %5u (%3u), free mpn = %5u (%3u)"
            " free blocks = %2u\n",
            count, ftl->free_vpn, free_vol_list_pgs(ftl), ftl->free_mpn, free_map_list_pgs(ftl),
            ftl->num_free_blks);
#endif

      // Break if enough pages have been freed.
      if (FtlnRecNeeded(ftl, wr_cnt) == FALSE)
        break;

#if FTLN_DEBUG_RECYCLES
      // If enabled, list the number free and the state of each block.
      if (ftl->flags & FTLN_VERBOSE) {
        printf("num_free=%d\n", ftl->num_free_blks);
        show_blks(ftl);
      }
#endif

      // Ensure we haven't recycled too many times.
      PfAssert(count <= 2 * ftl->num_blks);
      if (count > 2 * ftl->num_blks) {
#if FTLN_DEBUG
        printf("FTL NDM too many consec recycles = %u\n", count);
        FtlnBlkStats(ftl);
#endif
        return FsError2(FTL_RECYCLE_CNT, ENOSPC);
      }
    }
  }

  // Return success.
  return 0;
}

// FtlnMapGetPpn: Map virtual page number to its physical page number
//
//      Inputs: ftl = pointer to FTL control block
//              vpn = VPN to look up
//      Output: *pnp = physical page number or -1 if unmapped
//
//        Note: By causing a map cache page flush, this routine can
//              consume one free page
//
//     Returns: 0 on success, -1 on failure
//
int FtlnMapGetPpn(CFTLN ftl, ui32 vpn, ui32* pnp) {
  ui32 mpn, ppn;
  ui8* maddr;
  int unmapped;

  // Determine map page to use.
  PfAssert(vpn <= ftl->num_vpages);
  mpn = vpn / ftl->mappings_per_mpg;

  // Retrieve map page via cache. Return -1 if I/O error.
  maddr = ftlmcGetPage(ftl->map_cache, mpn, &unmapped);
  if (maddr == NULL)
    return -1;

  // If page is unmapped, set page number to -1.
  if (unmapped) {
    ppn = (ui32)-1;

    // Else get physical page number from map. Equals -1 if unmapped.
  } else {
    // Calculate address of VPN's entry in map page and read it.
    maddr += (vpn % ftl->mappings_per_mpg) * FTLN_PN_SZ;
    ppn = GET_MAP_PPN(maddr);

    // If physical page number is too big, it is unmapped.
    if (ppn >= ftl->num_pages)
      ppn = (ui32)-1;

#if FS_ASSERT
    // Else verify that it lies in a volume block.
    else {
      ui32 b = ppn / ftl->pgs_per_blk;

      PfAssert(!IS_MAP_BLK(ftl->bdata[b]) && !IS_FREE(ftl->bdata[b]));
    }
#endif
  }

  // Output physical page for VPN and return success.
  *pnp = ppn;
  return 0;
}

// FtlnMapSetPpn: Set new physical page number in given VSN's map page
//
//      Inputs: ftl = pointer to FTL control block
//              vpn = VPN to create new mapping for
//              ppn = new physical location for VPN or -1 if unmapped
//
//        Note: By causing a map cache page flush, this routine can
//              consume one free page
//
//     Returns: 0 on success, -1 on failure
//
int FtlnMapSetPpn(CFTLN ftl, ui32 vpn, ui32 ppn) {
  ui32 mpn;
  ui8* maddr;

  // Determine map page to use.
  PfAssert(vpn <= ftl->num_vpages);
  mpn = vpn / ftl->mappings_per_mpg;

  // Retrieve map page contents via cache, marking it dirty if clean.
  maddr = ftlmcGetPage(ftl->map_cache, mpn, NULL);
  if (maddr == NULL)
    return -1;

  // Calculate address of VSN's entry in map page.
  maddr += (vpn % ftl->mappings_per_mpg) * FTLN_PN_SZ;

  // Set physical page number for VPN and return success.
  SET_MAP_PPN(maddr, ppn);
  return 0;
}

//  FtlnVclean: Perform vclean() on FTL volume
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 if no more vclean needed, 1 if future vclean needed,
//              -1 on error
//
int FtlnVclean(FTLN ftl) {
  ui32 b;

  // Set errno and return -1 if fatal I/O error occurred.
  if (ftl->flags & FTLN_FATAL_ERR)
    return FsError2(NDM_EIO, EIO);

  // Check if the dirty pages garbage level is > 9.
  if (FtlnGarbLvl(ftl) >= 10) {
    // Perform one recycle operation. Return -1 if error.
    if (recycle(ftl, /*should_boost_low_wear=*/1))
      return -1;

    // Return '1' so that vclean() is called again.
    return 1;
  }

  // Look for a block that is free, but not erased.
  for (b = 0; b < ftl->num_blks; ++b) {
    if (IS_FREE(ftl->bdata[b]) && !IS_ERASED(ftl->bdata[b])) {
      // Erase block. Return -1 if error.
      if (FtlnEraseBlk(ftl, b))
        return -1;

      // Return '1' so that vclean() is called again.
      return 1;
    }
  }

  // Nothing to do, return '0'.
  return 0;
}

//   FtlnMapWr: Write a map page to flash - used by map page cache
//
//      Inputs: vol = pointer to FTL control block
//              mpn = map page to write
//              buf = pointer to page data buffer or NULL
//
//     Returns: 0 on success, -1 on error
//
int FtlnMapWr(void* vol, ui32 mpn, void* buf) {
  FTLN ftl = vol;
  ui32 pn, b, wc, old_pn = ftl->mpns[mpn];
  int status;

  // Return -1 if fatal I/O error occurred.
  if (ftl->flags & FTLN_FATAL_ERR)
    return FsError2(NDM_EIO, EIO);

#if INC_ELIST
  // If list of erased blocks/wear counts exists, erase it now.
  if (ftl->elist_blk != (ui32)-1) {
    if (FtlnEraseBlk(ftl, ftl->elist_blk))
      return -1;
  }
#endif

  // Allocate next free map page. Return -1 if error.
  pn = next_free_mpg(ftl);
  if (pn == (ui32)-1)
    return -1;

  // Determine the block's erase wear count.
  b = pn / ftl->pgs_per_blk;
  wc = ftl->high_wc - ftl->blk_wc_lag[b];
  PfAssert((int)wc > 0);

  // Initialize spare area, including VPN, block count, and wear count.
  memset(ftl->spare_buf, 0xFF, ftl->eb_size);
  SET_SA_VPN(mpn, ftl->spare_buf);
  SET_SA_BC(ftl->high_bc, ftl->spare_buf);
  SET_SA_WC(wc, ftl->spare_buf);

  // If page data in buffer, invoke write_page().
  if (buf) {
    ++ftl->stats.write_page;
    status = ndmWritePage(ftl->start_pn + pn, buf, ftl->spare_buf, ftl->ndm);

    // Else source data is in flash. Invoke page transfer routine.
  } else {
    ++ftl->stats.transfer_page;
    status = ndmTransferPage(ftl->start_pn + old_pn, ftl->start_pn + pn, ftl->main_buf,
                             ftl->spare_buf, ftl->ndm);
  }

  // I/O or ECC decode error is fatal.
  if (status)
    return FtlnFatErr(ftl);

  // If the meta page, invalidate pointer to physical location.
  if (mpn == ftl->num_map_pgs - 1) {
    ftl->mpns[mpn] = (ui32)-1;

    // Else adjust block used page counts.
  } else {
    // Increment page used count in new block.
    PfAssert(IS_MAP_BLK(ftl->bdata[b]));
    INC_USED(ftl->bdata[b]);

    // Set the MPN array entry with the new page number.
    ftl->mpns[mpn] = pn;

    // If page has an older copy, decrement used count on old block.
    if (old_pn != (ui32)-1)
      FtlnDecUsed(ftl, old_pn, mpn);
  }

  // Return success.
  return 0;
}

// FtlnGarbLvl: Calculate the volume garbage level
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: Calculated volume garbage level
//
ui32 FtlnGarbLvl(CFTLN ftl) {
  ui32 b, free_pages, used_pages = 0;

  // Count the number of used pages.
  for (b = 0; b < ftl->num_blks; ++b)
    if (!IS_FREE(ftl->bdata[b]))
      used_pages += NUM_USED(ftl->bdata[b]);

  // Count the number of free pages.
  free_pages = ftl->num_free_blks * ftl->pgs_per_blk;
  free_pages += free_vol_list_pgs(ftl);
  free_pages += free_map_list_pgs(ftl);

  // Garbage level is given by the following formula:
  //                        F
  //     GL = 100 x (1 -  -----)
  //                      T - U
  // where:
  //   F  = # of free pages in volume
  //   T  = # of pages in volume
  //   U  = # of used pages in volume
  // The result is a number in [0, 100) indicating percentage
  // of space that is dirty from the total available.
  return 100 - (100 * free_pages) / (ftl->num_pages - used_pages);
}

// FtlnGetWearHistogram: Get a 20 bucket histogram of wear counts.
//
//       Input: ftl = Pointer to FTL control block.
//            count = Number of buckets on the provided buffer.
//        histogram = Storage for the result.
//
//     Returns: O on success, -1 if the buffer is not large enough.
//
int FtlnGetWearHistogram(CFTLN ftl, int count, ui32* histogram) {
  const int kNumBuckets = 20;
  uint block;

  if (count < kNumBuckets) {
    return -1;
  }
  memset(histogram, 0, sizeof(ui32) * kNumBuckets);

  for (block = 0; block < ftl->num_blks; ++block) {
    ui8 value = 255 - ftl->blk_wc_lag[block];
    histogram[value * kNumBuckets / 256]++;
  }

  return 0;
}

#if FTLN_DEBUG_RECYCLES && FTLN_DEBUG_PTR
// FtlnShowBlks: Display block WC, selection priority, etc
//
void FtlnShowBlks(void) {
  semPend(FileSysSem, WAIT_FOREVER);
  show_blks(FtlnDebug);
  semPostBin(FileSysSem);
}
#endif
