// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl_private.h"
#include "ftlnp.h"

// Configuration
#define DEBUG_RESUME FALSE

// Symbol Definitions
#define COPY_BLK_END 0xFFFFFFFD
#define COPY_BLK_MARK 0xFFFFFFFE

// Global Variable Declarations
CircLink FtlnVols = {&FtlnVols, &FtlnVols};
#if FTLN_DEBUG_PTR
FTLN Ftln;
#endif
#ifdef FTL_RESUME_STRESS
extern int FtlMblkResumeCnt, FtlVblkResumeCnt;
#endif

// Local Function Definitions

#if INC_ELIST
//  proc_elist: Process elist map page
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: NDM_PAGE_VALID (1) or NDM_PAGE_INVALID (2)
//
static int proc_elist(FTLN ftl) {
  ui32 b, wc, *lp = (ui32*)(ftl->main_buf + FTLN_META_DATA_BEG);

  // Loop to process each block number/wear count entry in page.
  do {
    // Get number of proposed erased block and its wear count.
    b = RD32_LE(lp);
    ++lp;
    wc = RD32_LE(lp);
    ++lp;

    // List validly ends with -1.
    if (b > ftl->num_blks) {
      PfAssert(b == (ui32)-1);
      break;
    }

    // Check block's wear count.
    if ((wc > ftl->high_wc) || (ftl->high_wc - wc > 0xFF))
      return NDM_PAGE_INVALID;

    // Skip the elist block itself. It is definitely not erased.
    if (b != ftl->elist_blk) {
#if DEBUG_ELIST
      // Verify that this block is unwritten.
      FtlnCheckBlank(ftl, b);
#endif

      // Verify block is unused and not a map block.
      if (NUM_USED(ftl->bdata[b])) {
        ftl->logger.error("FTL block %u in erase list is invalid. Block contains %u used pages.", b,
                          NUM_USED(ftl->bdata[b]));
        return NDM_PAGE_INVALID;
      }

      if (IS_MAP_BLK(ftl->bdata[b])) {
        ftl->logger.error("FTL block %u in erase list is invalid. Block is marked as a map block.",
                          b);
        return NDM_PAGE_INVALID;
      }

      // If not already marked free, increment free block count.
      if (!IS_FREE(ftl->bdata[b]))
        ++ftl->num_free_blks;

      // Set block's state and wear count lag.
      ftl->bdata[b] = FREE_BLK_FLAG | ERASED_BLK_FLAG;
      ftl->blk_wc_lag[b] = ftl->high_wc - wc;
    }
  } while (lp < (ui32*)(ftl->main_buf + ftl->page_size));

  // Finished and no check failed. Page is valid.
  return NDM_PAGE_VALID;
}
#endif

// map_page_check: Check contents of map page for validity
//
//      Inputs: ftl = pointer to FTL control block
//              apn = absolute physical page number (+ ftl->start_pn)
//              process = do stored request if map page is meta-page
//
//     Returns: -1 if fatal error, else NDM_PAGE_ERASED (0),
//              NDM_PAGE_VALID (1), or NDM_PAGE_INVALID (2)
//
static int map_page_check(FTLN ftl, ui32 apn, int process) {
  ui32 mpn, n, *ppns = (ui32*)ftl->main_buf;
  int status;

  // Call driver validity check. Return -1 if error.
  ++ftl->stats.page_check;
  status = ndmCheckPage(apn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
  if (status < 0) {
    ftl->logger.error("Failed to check map page at %u page contents.", apn);
    return FtlnFatErr(ftl);
  }

  // If page is erased or invalid, return its status.
  if (status != NDM_PAGE_VALID) {
    return status;
  }

  // If MPN too big, page is invalid.
  mpn = GET_SA_VPN(ftl->spare_buf);
  if (mpn >= ftl->num_map_pgs) {
    ftl->logger.error(
        "Map page at %u page is not valid. Contains %u map page number with a maximum page number "
        "of %u.",
        apn, mpn, ftl->num_map_pgs);
    return NDM_PAGE_INVALID;
  }

  // If meta-page, check version, type, and format. Process if enabled.
  if (mpn == ftl->num_map_pgs - 1) {
    ui32 type, vers = RD32_LE(&ppns[0]);

    // Check if metapage version.
    if (vers == FTLN_META_VER1) {
      // Read the meta-page type.
      type = RD32_LE(&ppns[1]);

      // Check if 'continue format' metadata.
      if (type == CONT_FORMAT) {
        // Rest of meta-page should be erased.
        for (n = 2; n < ftl->page_size / sizeof(ui32); ++n)
          if (RD32_LE(&ppns[n]) != (ui32)-1) {
            ftl->logger.error(
                "Found meta page with type |CONT_FORMAT|, but rest of contents where not 0xFF.");
            return NDM_PAGE_INVALID;
          }

        // If enabled, resume the format.
        if (process)
          if (FtlnFormat(ftl, (apn - ftl->start_pn) / ftl->pgs_per_blk)) {
            ftl->logger.error("Failed to resume FTL format from meta page.");
            return -1;
          }
      }
#if INC_ELIST
      // Check if 'erased block list' metapage.
      else if (type == ERASED_LIST) {
        // Just save block number if called from build_map(). Called
        // once for each used page in the elist block.
        if (process == FALSE)
          ftl->elist_blk = (apn - ftl->start_pn) / ftl->pgs_per_blk;

        // Else read/check/process each elist page contents if caller
        // is meta_read(). Called once, using last elist page number.
        else {
          ui32 ap0 = ftl->start_pn + ftl->elist_blk * ftl->pgs_per_blk;

          // Process each elist page, from last to first.
          for (;;) {
            // Verify and apply elist page. Return if page invalid.
            status = proc_elist(ftl);
            if (status != NDM_PAGE_VALID) {
              ftl->logger.error("Failed to process erase block list.");
              return status;
            }

            // If first (perhaps only) page was processed, finished!
            if (apn == ap0)
              break;

              // Move to next written page in backwards direction. If
              // MLC flash, move to page whose pair has higher offset.
#if INC_FTL_NDM_SLC
            --apn;
#else
            for (;;) {
              ui32 pg_offset = --apn % ftl->pgs_per_blk;

              if (pg_offset == 0)
                break;
              if (ftl->pair_offset(pg_offset, ftl->ndm) >= pg_offset)
                break;
            }
#endif

            // Call driver to read/check next page. Return -1 if error.
            ++ftl->stats.page_check;
            status = ndmCheckPage(apn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
            if (status < 0) {
              ftl->logger.error("Page check at %u failed for map page.", apn);
              return FtlnFatErr(ftl);
            }

            // If page is erased or invalid, return its status.
            if (status != NDM_PAGE_VALID) {
              ftl->logger.warning("Erased or Invalid page found in erase block list.");
              return status;
            }

            // Verify the metadata version is correct.
            if (RD32_LE(&ppns[0]) != FTLN_META_VER1) {
              ftl->logger.error("Meta page contains invalid version. Found %u, expected %u.",
                                *(uint32_t*)(&ppns[0]), FTLN_META_VER1);
              return NDM_PAGE_INVALID;
            }

            // Verify the metadata type is correct.
            if (RD32_LE(&ppns[1]) != ERASED_LIST) {
              ftl->logger.error("Meta page of type |ERASED_LIST|.");
              return NDM_PAGE_INVALID;
            }
          }
        }
      }
#endif

      // Else meta page type is invalid.
      else
        return NDM_PAGE_INVALID;
    }

    // Else meta page version is invalid.
    else
      return NDM_PAGE_INVALID;
  }

  // Else regular map page.
  else {
    ui32 pn;
    ui8* maddr = ftl->main_buf;

    // Check every entry for validity.
    for (n = 0; n < ftl->mappings_per_mpg; ++n) {
      // Read entry's mapping from map page and update entry address.
      pn = GET_MAP_PPN(maddr);
      maddr += FTLN_PN_SZ;

      // Invalid page if entry is neither valid nor the unmapped value.
      if (pn >= ftl->num_pages && pn != UNMAPPED_PN) {
        ftl->logger.error(
            "Mapped page number %u in map page %u mapping number %u exceeds maximum page number  "
            "%u.",
            pn, apn, n, ftl->num_pages);
        return NDM_PAGE_INVALID;
      }
    }
  }

  // All checks passed! Page is valid.
  return NDM_PAGE_VALID;
}

//   build_map: Scan volume blocks and for map ones, read all valid
//              map pages to build the MPNs array
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 on success, -1 on error
//
static int build_map(FTLN ftl) {
  int status;
  ui32 b, bc, *bcs, mpn, n, pn, po, *b_ptr, map_block;

  // Allocate space to hold block count for each map page array entry.
  bcs = FsCalloc(ftl->num_map_pgs, sizeof(ui32));
  if (bcs == NULL)
    return FsError2(FTL_ENOMEM, ENOMEM);

  // Loop over every block looking for map blocks. This list was made
  // by format_status() and only has one with the highest BC, but may
  // include old map blocks that didn't get erased after their recycle.
  for (b = 0; b < ftl->num_blks; ++b) {
    map_block = b;
    // Skip blocks that don't hold any map pages.
    if (!IS_MAP_BLK(ftl->bdata[b]))
      continue;

    // Compute first page on block.
    pn = ftl->start_pn + b * ftl->pgs_per_blk;

    // For each page in map block, check if MPN array needs updating.
    for (po = 0, bc = (ui32)-1; po < ftl->pgs_per_blk; ++po, ++pn) {
#if INC_FTL_NDM_MLC
      // For MLC devices, skip pages not written by the FTL, those
      // whose pair offset is lower than their offset.
      if (ftl->pair_offset(po, ftl->ndm) < po)
        continue;
#endif

      // Check if page is on newest map block and not its first page.
      // The newest map block is only one that potentially has (as its
      // partially written last page) an invalid page. Look for that.
      if (po && bc == ftl->high_bc) {
        // Check if page contents are valid. Return -1 if fatal error.
        status = map_page_check(ftl, pn, FALSE);
        if (status < 0) {
          FsFree(bcs);
          return -1;
        }

        // If invalid last page, break to advance to next map block.
        if (status == NDM_PAGE_INVALID)
          break;

        // Else erased last page, break to advance to next map block.
        else if (status == NDM_PAGE_ERASED)
          break;

        // Remember highest valid map page on most recent map block.
        ftl->high_bc_mblk_po = po;
      }

      // Else page on older map block or first on newest map block.
      else {
        // Read page's spare area.
        ++ftl->stats.read_spare;
        status = ndmReadSpare(pn, ftl->spare_buf, ftl->ndm);

        // Return if fatal error.
        if (status == -2) {
          FsFree(bcs);
          return FtlnFatErr(ftl);
        }

        // Break to skip block if uncorrectable ECC error occurred.
        if (status < 0)
          break;
      }

      // If first page, retrieve block count. Otherwise compare with
      // block count of block's already-checked-valid first page.
      if (po == 0) {
        bc = GET_SA_BC(ftl->spare_buf);
      } else if (bc != GET_SA_BC(ftl->spare_buf)) {
#if FTLN_DEBUG > 1
        ftl->logger.debug("build_map: b = %u, po = %u, i_bc = %u vs 0_bc = %u", b, po,
                          GET_SA_BC(ftl->spare_buf), bc);
#endif

        // Should not be, but page is invalid. Break to skip block.
        break;
      }

      // Block count is retrieved by now.
      PfAssert(bc != (ui32)-1);

      // Adjust map block read count.
      b_ptr = &ftl->bdata[b];
      INC_RC(ftl, b_ptr, 1);

      // Retrieve MPN and check that it is valid.
      mpn = GET_SA_VPN(ftl->spare_buf);
      if (mpn > ftl->num_map_pgs) {
#if FTLN_DEBUG > 1
        ftl->logger.debug("build_map: b = %u, po = %u, mpn = %u, max = %u", b, po, mpn,
                          ftl->num_map_pgs);
#endif

        // Should not be, but page is invalid. Break to skip block.
        break;
      }

      // If no entry for this MPN in array OR entry in same block as
      // current block OR entry in a block with a lower block count,
      // update array entry with current page.
      if (ftl->mpns[mpn] == (ui32)-1 || ftl->mpns[mpn] / ftl->pgs_per_blk == b || bcs[mpn] < bc) {
        // If not metapage, adjust used counts of referenced blks.
        if (mpn < ftl->num_map_pgs - 1) {
          // If old MPN array entry already set, decrement old block's
          // used pages count.
          if (ftl->mpns[mpn] != (ui32)-1) {
            uint ob = ftl->mpns[mpn] / ftl->pgs_per_blk;

            PfAssert(IS_MAP_BLK(ftl->bdata[ob]));
            DEC_USED(ftl->bdata[ob]);
          }

          // Increment used count for new block.
          PfAssert(IS_MAP_BLK(ftl->bdata[b]));
          INC_USED(ftl->bdata[b]);
        }
#if FTLN_DEBUG > 1
        ftl->logger.debug("build_map: mpn = %u, old_pn = %d, new_pn = %u", mpn, ftl->mpns[mpn],
                          b * ftl->pgs_per_blk + po);
#endif

        // Save the map page number and (temporarily) the block count.
        ftl->mpns[mpn] = b * ftl->pgs_per_blk + po;
        bcs[mpn] = bc;
      }
    }
  }

  // Free temporary block counts space.
  FsFree(bcs);

#if INC_ELIST
  // If present, change state of elist block from map block to free.
  if (ftl->elist_blk != (ui32)-1) {
    ftl->bdata[ftl->elist_blk] = FREE_BLK_FLAG;
    ++ftl->num_free_blks;
  }
#endif

  // Loop over map blocks to build volume block's used page counts.
  for (mpn = 0; mpn < ftl->num_map_pgs - 1; ++mpn) {
    ui8* maddr;

    // Skip unused map pages.
    pn = ftl->mpns[mpn];
    if (pn == (ui32)-1)
      continue;

#if FTLN_DEBUG > 1
    ftl->logger.debug("  -> MPN[%2u] = %u", mpn, pn);
#endif

    // Read map page. Return -1 if error.
    if (FtlnRdPage(ftl, pn, ftl->main_buf))
      return -1;

    // Loop over every physical page number entry on map page.
    maddr = ftl->main_buf;
    for (n = 0; n < ftl->mappings_per_mpg; ++n) {
      // Read entry's mapping from map page and update entry address.
      pn = GET_MAP_PPN(maddr);
      maddr += FTLN_PN_SZ;

      // Continue if no mapping at this entry.
      if (pn >= ftl->num_pages)
        continue;

      // Get page's block number and verify its status.
      b = pn / ftl->pgs_per_blk;

      if (IS_FREE(ftl->bdata[b])) {
        ftl->logger.error(
            "Map Page %u at %u contains maping offset %u mapped to physical %u. But physical block "
            "%u looks free.",
            mpn, map_block, mpn * ftl->mappings_per_mpg + n, pn, b);
        return -1;
      }

      if (IS_MAP_BLK(ftl->bdata[b])) {
        ftl->logger.error(
            "Map Page %u at %u contains maping offset %u mapped to physical %u. But physical block "
            "%u looks like a map block.",
            mpn, map_block, mpn * ftl->mappings_per_mpg + n, pn, b);
        return -1;
      }

      // Increment the used page count for this volume block.
      INC_USED(ftl->bdata[b]);

      // Record the highest used page offset in block's read count.
      po = pn % ftl->pgs_per_blk;
      if (po > GET_RC(ftl->bdata[b]))
        SET_RC(ftl->bdata[b], po);
    }
  }

  // If not recovered from the copy-end page (after interrupted vblk
  // resume), find the volume block with the lowest used page offset.
  if (ftl->copy_end_found == FALSE) {
    ftl->resume_po = ftl->pgs_per_blk;
    for (b = 0; b < ftl->num_blks; ++b) {
      if (NUM_USED(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b])) {
        po = GET_RC(ftl->bdata[b]);
        if (po < ftl->resume_po) {
          ftl->resume_vblk = b;
          ftl->resume_po = po;
          if (po == 0)
            break;
        }
      }
    }
  }
#if FTLN_DEBUG > 1
  ftl->logger.debug("volume block %d has lowest used page offset (%d)\n", ftl->resume_vblk,
                    ftl->resume_po);
#endif

  // Clean temporary use of vol block read-wear field for page offset.
  for (b = 0; b < ftl->num_blks; ++b)
    if (NUM_USED(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b]))
      ftl->bdata[b] &= (ui32)~RC_MASK;

  // Return success.
  return 0;
}

//  set_wc_lag: Set block's wear count lag and possibly adjust the
//              highest/lowest overall wear counts
//
//      Inputs: ftl = pointer to FTL control block
//              b = block number
//              wc = wear count for block
//         I/O: *low_wc = lowest wear count encountered so far
//
static void set_wc_lag(FTLN ftl, ui32 b, ui32 wc, ui32* low_wc) {
  // If this block has lowest wear count, update lowest.
  if (*low_wc > wc)
    *low_wc = wc;

  // If it has highest wear count, update highest and also update wear
  // count offsets of all used (not free) blocks below it.
  if (wc > ftl->high_wc) {
    ui32 lb, increase = wc - ftl->high_wc;

    // Loop over all lower numbered blocks.
    for (lb = 0; lb < b; ++lb) {
      // Skip blocks that don't have a valid wear count value.
      if (GET_RC(ftl->bdata[lb]) == 100)
        continue;

      // Update previously set wear count lags, avoiding ui8 overflow.
      if (ftl->blk_wc_lag[lb] + increase > 0xFF) {
        ftl->blk_wc_lag[lb] = 0xFF;
        ++ftl->wear_data.max_wc_over;
      } else
        ftl->blk_wc_lag[lb] += increase;
    }

    // Remember new high wear count.
    ftl->high_wc = wc;
  }

  // Set block wear count lag, avoiding ui8 overflow.
  if (ftl->high_wc - wc > 0xFF) {
    ftl->blk_wc_lag[b] = 0xFF;
    ++ftl->wear_data.max_wc_over;
  } else
    ftl->blk_wc_lag[b] = ftl->high_wc - wc;
}

// format_status: Check if FTL volume is formatted
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: TRUE if formatted, FALSE if unformatted, -1 if error
//
static int format_status(FTLN ftl) {
  ui32 b, n, avg_lag, pn, bc, wc, low_wc = (ui32)-1;
  int rc, formatted = FALSE;

  // Scan first page on all blocks to determine block status.
  for (ftl->num_free_blks = b = 0; b < ftl->num_blks; ++b) {
    // Compute page number of block's first page.
    pn = ftl->start_pn + b * ftl->pgs_per_blk;

    // Read spare area for first page. Return -1 if fatal error.
    ++ftl->stats.read_spare;
    rc = ndmReadSpare(pn, ftl->spare_buf, ftl->ndm);
    if (rc == -2) {
      ftl->logger.error("Failed to obtain spare area contents for page %u", pn);
      return FtlnFatErr(ftl);
    }

    // Read metadata from spare area.
    bc = GET_SA_BC(ftl->spare_buf);
    wc = GET_SA_WC(ftl->spare_buf);

    // Check if the block count is 0xFFFFFFFF.
    if (bc == 0xFFFFFFFF) {
      // If spare data looks erased, mark block as free.
      if (wc == 0x0FFFFFFF) {
        ftl->bdata[b] = FREE_BLK_FLAG;
        ++ftl->num_free_blks;
        PfAssert(GET_RC(ftl->bdata[b]) != 100);
        SET_RC(ftl->bdata[b], 100);  // flag to use average wear count
      }

      // Else classify as volume block.
      else {
        // If its wear count is in expected range, record it.
        if ((wc <= ftl->high_wc + 32) && ((wc + 32 >= low_wc) || (low_wc == (ui32)-1)))
          set_wc_lag(ftl, b, wc, &low_wc);

        // Else only use wear count if block has other non-empty pages
        // with same BC/wear, to discard partially written counts.
        else
          for (n = 1;;) {
            ui32 bc2, wc2;

            // Read spare area for higher page. Return -1 if fatal error.
            ++ftl->stats.read_spare;
            rc = ndmReadSpare(pn + n, ftl->spare_buf, ftl->ndm);
            if (rc == -2) {
              ftl->logger.error("Failed to obtain spare area contents for page %u", pn);
              return FtlnFatErr(ftl);
            }

            // If read good and counts match, set block wear count lag.
            bc2 = GET_SA_BC(ftl->spare_buf);
            wc2 = GET_SA_WC(ftl->spare_buf);
            if ((rc == 0) && (bc == bc2) && (wc == wc2)) {
              set_wc_lag(ftl, b, wc, &low_wc);
              break;
            }

#if INC_FTL_NDM_MLC
            // For MLC, try first next page that has no earlier pair, in
            // case FTL skipped volume pages for sync operation.
            if (n == 1) {
              n = ndmPastPrevPair(ftl->ndm, pn + 1) - pn;
              if (n != 1)
                continue;
            }
#endif

            // Done. Mark block as needing average wear count and break.
            PfAssert(GET_RC(ftl->bdata[b]) != 100);
            SET_RC(ftl->bdata[b], 100);
            break;
          }
      }
    }

    // Else check if this is an interrupted volume block transfer.
    else if (bc == COPY_BLK_MARK) {
#if DEBUG_RESUME
      puts("encountered copy-blk mark");
#endif
      // Call driver validity check. Return -1 if error.
      ++ftl->stats.page_check;
      rc = ndmCheckPage(pn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
      if (rc < 0) {
        ftl->logger.error("Failed to check physical page %u contents.", pn);
        return FtlnFatErr(ftl);
      }

      // If page is invalid, mark block free and continue.
      if (rc != NDM_PAGE_VALID) {
        ftl->bdata[b] = FREE_BLK_FLAG;
        ++ftl->num_free_blks;
        PfAssert(GET_RC(ftl->bdata[b]) != 100);
        SET_RC(ftl->bdata[b], 100);  // flag to use average wear count
        continue;
      }

      // Set block wear count lag.
      set_wc_lag(ftl, b, wc, &low_wc);

      // Search for copy-end page, indicating the 'copy to' finished.
      for (n = 1; n < ftl->pgs_per_blk; ++n) {
        ui32 vpn;

        // Read spare data. Return if fatal error. Skip if ECC error.
        ++ftl->stats.read_spare;
        rc = ndmReadSpare(pn + n, ftl->spare_buf, ftl->ndm);
        if (rc == -2) {
          ftl->logger.error("Failed to read spare data for physical page %u.", pn + n);
          return FtlnFatErr(ftl);
        }
        if (rc)
          continue;

        // Read metadata from spare area.
        vpn = GET_SA_VPN(ftl->spare_buf);
        bc = GET_SA_BC(ftl->spare_buf);
        wc = GET_SA_WC(ftl->spare_buf);

        // Check if this is the copy-end page.
        if ((vpn == COPY_BLK_END) && (bc == vpn) && (wc == 0)) {
#if DEBUG_RESUME
          puts("encountered copy-blk end");
#endif
          // Read and check the copy-end page. Return -1 if error.
          ++ftl->stats.page_check;
          rc = ndmCheckPage(pn + n, ftl->main_buf, ftl->spare_buf, ftl->ndm);
          if (rc < 0) {
            ftl->logger.error("Failed to check page contents for physical page %u.", pn + n);
            return FtlnFatErr(ftl);
          }

          // Break if page is invalid.
          if (rc != NDM_PAGE_VALID)
            break;

          // Flag that the copy-end page has been found.
          ftl->copy_end_found = TRUE;

          // Save parameters of the interrupted vblk resume transfer.
          ftl->resume_vblk = RD32_LE(&ftl->main_buf[0]);
          PfAssert(ftl->resume_vblk < ftl->num_blks);
          ftl->resume_tblk = b;
          ftl->resume_po = n - 1;
#if DEBUG_RESUME
          {
            ui32 vb = ftl->resume_vblk;
            const char* filler = "";
            if (IS_FREE(ftl->bdata[vb])) {
              filler = "free block.";
            } else if (IS_MAP_BLK(ftl->bdata[vb])) {
              filler = "map block.";
            }

            ftl->logger.debug("Resume_vblk=%d bdata=0x%X %s", vb, ftl->bdata[vb], filler);
          }
#endif

          // Mark the resume temporary block free and break.
          ftl->bdata[b] = FREE_BLK_FLAG;
          ++ftl->num_free_blks;
          break;
        }
      }

      // Check if copy-end page was not found.
      if (!ftl->copy_end_found) {
        // Return if doing a read-only initialization.
        if (ftl->flags & FSF_READ_ONLY_INIT) {
          ftl->logger.error("FTL format cannot be applied on read only initialization.");
          return FsError2(FTL_VOL_BLK_XFR, EINVAL);
        }

        // Erase block. Return -1 if I/O error.
        if (FtlnEraseBlk(ftl, b)) {
          return -1;
        }
      }
    }

    // Else this looks like a map block.
    else {
      // Check block's first map page for validity. Return -1 if error.
      rc = map_page_check(ftl, pn, FALSE);
      if (rc < 0) {
        ftl->logger.error("Map page check failed on physical page %u.", pn);
        return -1;
      }

      // If first page is invalid, whole block is invalid. Free it.
      if (rc != NDM_PAGE_VALID) {
        ftl->bdata[b] = FREE_BLK_FLAG;
        ++ftl->num_free_blks;
        PfAssert(GET_RC(ftl->bdata[b]) != 100);
        SET_RC(ftl->bdata[b], 100);  // flag to use average wear count
      }

      // Else this is a valid map page and block.
      else {
        // Remember that volume is formatted. Mark block as map block.
        formatted = TRUE;
        SET_MAP_BLK(ftl->bdata[b]);  // clear used/read pages cnt

        // Set block wear count lag.
        set_wc_lag(ftl, b, wc, &low_wc);

        // If this is the highest block count so far, remember it.
        if (ftl->high_bc < bc) {
          ftl->high_bc = bc;
          ftl->high_bc_mblk = b;
        }

        // Else if this is the second block with highest block count,
        // it's an interrupted map block transfer.
        else if (ftl->high_bc == bc && ftl->high_bc_mblk != (ui32)-1) {
          // Return if doing a read-only initialization.
          if (ftl->flags & FSF_READ_ONLY_INIT) {
            ftl->logger.error("FTL format cannot be applied on read only initialization.");
            return FsError2(FTL_MAP_BLK_XFR, EINVAL);
          }

          // Erase block that was destination of interrupted transfer.
          if (ftl->blk_wc_lag[b] > ftl->blk_wc_lag[ftl->high_bc_mblk]) {
            rc = FtlnEraseBlk(ftl, ftl->high_bc_mblk);
            ftl->high_bc_mblk = b;
          } else
            rc = FtlnEraseBlk(ftl, b);
          if (rc)
            return -1;
        }
      }
    }
  }

  // If volume is unformatted, return FALSE.
  if (formatted == FALSE) {
    ftl->logger.info("No FTL Volume found.");
    return FALSE;
  }

  // Compute the average 'high_wc' lag.
  for (avg_lag = n = b = 0; b < ftl->num_blks; ++b)
    if (GET_RC(ftl->bdata[b]) != 100) {
      avg_lag += ftl->blk_wc_lag[b];
      ++n;
    }
  if (n)
    avg_lag = (avg_lag + n / 2) / n;

  int wear_lag_histogram[256] = {0};
  int set_to_avg = 0;

  // Apply average wear offset to every block marked as needing it.
  for (b = 0; b < ftl->num_blks; ++b) {
    if ((ftl->bdata[b] & RC_MASK) == 100) {
      ftl->bdata[b] &= ~RC_MASK;
      ftl->blk_wc_lag[b] = avg_lag;
      ++set_to_avg;
    }
    ++wear_lag_histogram[ftl->blk_wc_lag[b]];
  }

  ftl->logger.info("Wear Count Range [%u, %u]", low_wc, ftl->high_bc);
  ftl->logger.info("Wear Count Average %u", ftl->high_bc - avg_lag);
  ftl->logger.info("Blocks with Wear Count[=%u]: %u", ftl->high_wc - 255,
                   ftl->wear_data.max_wc_over);
  ftl->logger.info("Blocks with estimated wear count: %u", set_to_avg);
  ftl->logger.info("Wear Lag Histogram: ");

  // 8 numbers per row, 5 characters per number = 40;
  char* line_buffer = FsCalloc(1, 40 + 1);

  if (line_buffer == NULL) {
    ftl->logger.error("Failed to allocate memory for FTL histogram line buffer.");
    return -1;
  }

  for (int i = 0; i < 256; ++i) {
    uint32_t column = i % 8;
    sprintf(line_buffer + 5 * column, "%5u", wear_lag_histogram[255 - i]);
    if (column == 7) {
      ftl->logger.info(line_buffer);
    }
  }

  FsFree(line_buffer);

  // Depending when powerfail recovery was interrupted, at this point
  // the volume block being resumed might look like a free block or a
  // volume block. Need it to be a volume block.
  if (ftl->copy_end_found) {
    PfAssert(!IS_MAP_BLK(ftl->bdata[ftl->resume_vblk]));
    if (IS_FREE(ftl->bdata[ftl->resume_vblk])) {
      ftl->bdata[ftl->resume_vblk] = 0;
      --ftl->num_free_blks;
    }
  }

  // Volume is formatted, return TRUE.
  PfAssert(ftl->num_free_blks < ftl->num_blks);
  return TRUE;
}

//   meta_read: Read FTL meta information page
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 if successful, else -1 for I/O error
//
static int meta_read(FTLN ftl) {
  ui32 pn = ftl->mpns[ftl->num_map_pgs - 1];

  // If no meta page, return 0.
  if (pn >= ftl->num_pages)
    return 0;

  // Read meta page. check/process its contents. Return -1 if error.
  if (map_page_check(ftl, ftl->start_pn + pn, TRUE) < 0) {
    ftl->logger.error("FTL map page check failed on meta page at %u.", ftl->start_pn + pn);
    return -1;
  }

  // Mark meta page invalid since no longer needed. Return success.
  ftl->mpns[ftl->num_map_pgs - 1] = (ui32)-1;
  return 0;
}

// copy_end_mark: Write the copy-end page, marking completion of the
//                copy from the volume block to the temporary block
//
//      Inputs: ftl = pointer to FTL control block
//              b = block number of the vblk resume temporary block
//
static int copy_end_mark(CFTLN ftl, ui32 b) {
  ui32 pn = ftl->start_pn + b * ftl->pgs_per_blk + ftl->resume_po + 1;

  // Page data is number of volume block with lowest used page offset.
  memset(ftl->main_buf, 0xFF, ftl->page_size);
  WR32_LE(ftl->resume_vblk, &ftl->main_buf[0]);

  // Initialize spare area, including VPN and block/wear counts.
  memset(ftl->spare_buf, 0xFF, ftl->eb_size);
  SET_SA_VPN(COPY_BLK_END, ftl->spare_buf);
  SET_SA_BC(COPY_BLK_END, ftl->spare_buf);
  SET_SA_WC(0, ftl->spare_buf);

  // Write page that marks the end of a volume resume copy block.
  return ndmWritePage(pn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
}

// resume_copy: Copy one volume block
//
//      Inputs: ftl = pointer to FTL control block
//              src_b = number of block to copy from
//              dst_b = number of block to copy to
//              bc = block count value: 0xFFFFFFFF or COPY_BLK_MARK
//
//     Returns: 0 on success, -1 on error
//
static int resume_copy(FTLN ftl, ui32 src_b, ui32 dst_b, ui32 bc) {
  int rc;
  ui32 po, vpn, wc;
  ui32 src_pg0 = ftl->start_pn + src_b * ftl->pgs_per_blk;
  ui32 dst_pg0 = ftl->start_pn + dst_b * ftl->pgs_per_blk;

  // Get the wear count of the source block.
  wc = ftl->high_wc - ftl->blk_wc_lag[src_b];
  PfAssert((int)wc > 0);

  // Copy all used pages from selected volume block to free block.
  for (po = 0; po <= ftl->resume_po; ++po) {
    // Read source page's spare area.
    ++ftl->stats.read_spare;
    rc = ndmReadSpare(src_pg0 + po, ftl->spare_buf, ftl->ndm);

    // Return -1 if fatal error, skip page if ECC error on spare read.
    if (rc) {
      if (rc == -2)
        return FtlnFatErr(ftl);
      else
        continue;
    }

    // Get virtual page number from spare. Skip page if out of range.
    vpn = GET_SA_VPN(ftl->spare_buf);
    if (vpn > ftl->num_vpages)
      continue;

    // Initialize spare area, including VPN and block/wear counts.
    memset(ftl->spare_buf, 0xFF, ftl->eb_size);
    SET_SA_VPN(vpn, ftl->spare_buf);
    SET_SA_BC(bc, ftl->spare_buf);
    SET_SA_WC(wc, ftl->spare_buf);

    // Invoke page transfer routine. If error, return -1.
    ++ftl->stats.transfer_page;
    if (ndmTransferPage(src_pg0 + po, dst_pg0 + po, ftl->main_buf, ftl->spare_buf, ftl->ndm)) {
      ftl->logger.error("FTL failed to transfer page %u to page %u.", src_pg0 + po, dst_pg0 + po);
      return FtlnFatErr(ftl);
    }
  }

  // Return success.
  return 0;
}

//   init_ftln: Prepare a TargetFTL-NDM volume for use
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 on success, -1 on failure
//
static int init_ftln(FTLN ftl) {
  int formatted;
  ui32 b, n;

  // Analyze volume to see if it is formatted. Return -1 if error.
  formatted = format_status(ftl);
  if (formatted < 0) {
    ftl->logger.error("Failed to verify FTL format status.");
    return -1;
  }

  // If unformatted, blocks are free w/zero 'high_wc' lag.
  if (formatted == FALSE) {
    // Return if doing a read-only initialization.
    if (ftl->flags & FSF_READ_ONLY_INIT) {
      ftl->logger.error("FTL format aborted on read only initialization.");
      return FsError2(FTL_NO_MAP_BLKS, EINVAL);
    }

    // Record all blocks as free with zero 'high_wc' lag.
    for (b = 0; b < ftl->num_blks; ++b) {
      ftl->blk_wc_lag[b] = 0;
      ftl->bdata[b] = FREE_BLK_FLAG;
    }
    ftl->num_free_blks = ftl->num_blks;
    ftl->high_bc = 1;  // initial block count of unformatted volumes
    return 0;
  }

  // Look for all the valid map pages on all the map blocks.
  if (build_map(ftl)) {
    ftl->logger.error("FTL failed to initialize mapping from media.");
    return -1;
  }

  // If below limit, convert unused volume blocks to free blocks.
  if (ftl->num_free_blks < FTLN_MIN_FREE_BLKS)
    for (b = 0; b < ftl->num_blks; ++b) {
      if (ftl->bdata[b] == 0)  // map/free flags clear and no use counts
      {
        ftl->bdata[b] = FREE_BLK_FLAG;
        ++ftl->num_free_blks;
      }
    }

  // Read and process meta page, if any. Return -1 if error.
  if (meta_read(ftl) < 0) {
    ftl->logger.error("FTL failed to read meta page.");
    return -1;
  }

  // Look for unused map blocks.
  for (b = 0; b < ftl->num_blks; ++b)
    if (IS_MAP_BLK(ftl->bdata[b]) && (NUM_USED(ftl->bdata[b]) == 0)) {
      // Return if doing a read-only initialization.
      if (ftl->flags & FSF_READ_ONLY_INIT) {
        ftl->logger.error("FTL format aborted on read only initialization.");
        return FsError2(FTL_UNUSED_MBLK, EINVAL);
      }

      // Erase unused map block.
      if (FtlnEraseBlk(ftl, b)) {
        ftl->logger.error("FTL failed to clean up unused map blocks.");
        return -1;
      }
    }

  // If free block count is below reserved number, a recycle has been
  // interrupted by a power failure. Must avoid losing additional free
  // blocks from additional power failures. Resume restores the free
  // map and volume page lists by copying valid entries to an erased
  // block, ensuring they don't have undetectable corruption from an
  // interrupted page write or block erase command. If resume is
  // interrupted by a power failure, no free blocks are lost.
  if (ftl->num_free_blks < FTLN_MIN_FREE_BLKS) {
#if DEBUG_RESUME
    ftl->logger.debug("Resuming: %u free blocks", ftl->num_free_blks);
    ftl->logger.debug("Map Block %u has used page offset of %u/%u", ftl->high_bc_mblk,
                      ftl->high_bc_mblk_po, ftl->pgs_per_blk);
    ftl->logger.debug("vol block %u has used page offset of %u/%u", ftl->resume_vblk,
                      ftl->resume_po, ftl->pgs_per_blk);
#endif

    // Resume needs one free block and should have it.
    PfAssert(ftl->num_free_blks >= 1);
    if (ftl->num_free_blks < 1) {
      ftl->logger.error("FTL initialization aborted due to lack of free blocks.");
      return -1;
    }

    // Check if low page-offset volume block has unused pages.
    if (ftl->resume_po < ftl->pgs_per_blk - 1) {
#ifdef FTL_RESUME_STRESS
      ++FtlVblkResumeCnt;
#endif

      // Return if doing a read-only initialization.
      if (ftl->flags & FSF_READ_ONLY_INIT) {
        ftl->logger.error("FTL format aborted on read only initialization.");
        return FsError2(FTL_VBLK_RESUME, EINVAL);
      }

      // Get the number of used pages on the volume block.
      n = NUM_USED(ftl->bdata[ftl->resume_vblk]);

      // If volume block transfer was interrupted, but the 'copy to'
      // finished, use the discovered 'copy to' block.
      if (ftl->copy_end_found) {
        b = ftl->resume_tblk;
        --ftl->num_free_blks;
      }

      // Else get a free block and copy the volume block to it.
      else {
        // Find free block w/highest wear count. Error if none free.
        b = FtlnHiWcFreeBlk(ftl);
        if (b == (ui32)-1) {
          ftl->logger.error("FTL did not find any free blocks with high wear count.");
          return -1;
        }

        // If the block is unerased, erase it now. Return -1 if error.
        if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
          if (FtlnEraseBlk(ftl, b)) {
            return -1;
          }

        // Decrement free block count.
        --ftl->num_free_blks;

        // Copy used pages to temp block.
        if (resume_copy(ftl, ftl->resume_vblk, b, COPY_BLK_MARK)) {
          ftl->logger.error("FTL failed to resume copy of block %u to temp block %u.",
                            ftl->resume_vblk, b);
          return -1;
        }

        // Write "end of copy" mark on next temp block page.
        if (copy_end_mark(ftl, b)) {
          ftl->logger.error("FTL failed write copy end mark at block %u.", b);
          return -1;
        }
      }

      // Erase the volume block with the lowest used page-offset.
      if (FtlnEraseBlk(ftl, ftl->resume_vblk)) {
        return -1;
      }

      // Copy the temp block's contents back to the volume block.
      if (resume_copy(ftl, b, ftl->resume_vblk, 0xFFFFFFFF)) {
        ftl->logger.error("FTL failed to copy from temp block %u to final block %u.", b,
                          ftl->resume_vblk);
        return -1;
      }

      // Mark resumed block as a volume block with 'n' used pages.
      ftl->bdata[ftl->resume_vblk] = n << 20;  // clr free & erased flags

      // Erase the temp copy block.
      if (FtlnEraseBlk(ftl, b)) {
        return -1;
      }

      // Assign the resumed ftl->free_vpn value.
      ftl->free_vpn = ftl->resume_vblk * ftl->pgs_per_blk + ftl->resume_po + 1;
    }

    // Check if high-block-count map block has unused pages.
    if (ftl->high_bc_mblk_po < ftl->pgs_per_blk - 1) {
#ifdef FTL_RESUME_STRESS
      ++FtlMblkResumeCnt;
#endif

      // Return if doing a read-only initialization.
      if (ftl->flags & FSF_READ_ONLY_INIT) {
        ftl->logger.error("FTL format aborted on read only initialization.");
        return FsError2(FTL_MBLK_RESUME, EINVAL);
      }

      // Find free block with lowest wear count. Error if none free.
      b = FtlnLoWcFreeBlk(ftl);
      if (b == (ui32)-1) {
        ftl->logger.error("FTL did not find any free blocks with low wear count.");
        return -1;
      }

      // If the block is unerased, erase it now. Return -1 if error.
      if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
        if (FtlnEraseBlk(ftl, b)) {
          ftl->logger.error("FTL failed to erase free block at %u.", b);
          return -1;
        }

      // Decrement free block count.
      --ftl->num_free_blks;

      // Set free MPN pointer to first page in block (wo BC increment).
      ftl->free_mpn = b * ftl->pgs_per_blk;

      // Clear free block flag and read count, set map block flag.
      SET_MAP_BLK(ftl->bdata[b]);  // clr free flag & read wear count

      // Set wear count of copy to be one higher than source block.
      if (ftl->blk_wc_lag[ftl->high_bc_mblk])
        ftl->blk_wc_lag[b] = ftl->blk_wc_lag[ftl->high_bc_mblk] - 1;
      else {
        ftl->blk_wc_lag[ftl->high_bc_mblk] = 1;
        ftl->blk_wc_lag[b] = 0;
      }

      // Copy the used pages to a free block, then erase the original.
      if (FtlnRecycleMapBlk(ftl, ftl->high_bc_mblk)) {
        ftl->logger.error("FTL failed to recycle block at %u to free up unused pages.",
                          ftl->high_bc_mblk);
        return -1;
      }
    }
  }

#if FTLN_DEBUG > 1
  ftl->logger.debug(
      "FTL formatted successfully. [Current Generation Number = %u  Highest Wear Count = %u",
      ftl->high_bc, ftl->high_wc);
#endif

  // Do recycles if needed and return status.
  return FtlnRecCheck(ftl, 0);
}

//    free_ftl: Free FTL memory allocations
//
//       Input: vol = FTL handle
//
static void free_ftl(void* vol) {
  FTLN ftl = vol;

#if FTLN_DEBUG > 1
  // Display FTL statistics.
  FtlnStats(ftl);
#endif

  // Free FTL memory allocations.
  if (ftl->bdata)
    FsFree(ftl->bdata);
  if (ftl->blk_wc_lag)
    FsFree(ftl->blk_wc_lag);
  if (ftl->mpns)
    FsFree(ftl->mpns);
  if (ftl->main_buf)
    FsAfreeClear(&ftl->main_buf);
  if (ftl->map_cache)
    ftlmcDelete(&ftl->map_cache);
  FsFree(ftl);
}

//  rd_map_pg: Read an MPN from flash - used by MPN cache
//
//      Inputs: vol = FTL handle
//              mpn = map page to read
//              buf = buffer to hold contents of map page
//      Output: *unmapped = TRUE iff page is unmapped
//
//     Returns: 0 on success, -1 on error
//
static int rd_map_pg(void* vol, ui32 mpn, void* buf, int* unmapped) {
  FTLN ftl = vol;
  ui32 ppn;

  // Sanity check that map page index is valid and not the meta page.
  PfAssert(mpn < ftl->num_map_pgs - 1);

  // Retrieve physical map page number from MPNs array, if available.
  // Else output 0xFF's, set unmapped flag, and return success.
  ppn = ftl->mpns[mpn];
  if (ppn == (ui32)-1) {
    memset(buf, 0xFF, ftl->page_size);
    if (unmapped)
      *unmapped = TRUE;
    return 0;
  }

  // If output pointer provided, mark page as mapped.
  if (unmapped)
    *unmapped = FALSE;

  // Read page from flash and return status.
  return FtlnRdPage(ftl, ppn, buf);
}

// Global Function Definitions

// FtlnAddVol: Create a new FTL volume
//
//      Inputs: ftl_cfg = pointer to FTL configuration structure
//              xfs = pointer to FTL interface structure
//
//     Returns: FTL handle on success, NULL on error.
//
void* FtlnAddVol(FtlNdmVol* ftl_cfg, XfsVol* xfs) {
  ui32 n, vol_blks;
  ui8* buf;
  FTLN ftl;

  // If number of blocks less than 7, FTL-NDM cannot work.
  if (ftl_cfg->num_blocks < 7) {
    ftl_cfg->logger.error("Invalid Arguments. FTL requires at least 7 blocks to work.");
    FsError2(FTL_CFG_ERR, EINVAL);
    return NULL;
  }

  // Ensure FTL flags are valid.
  if (ftl_cfg->flags & ~(FSF_EXTRA_FREE | FSF_READ_WEAR_LIMIT | FSF_READ_ONLY_INIT)) {
    ftl_cfg->logger.error("Invalid Arguments. FTL config contains unknown flags.");
    FsError2(FTL_CFG_ERR, EINVAL);
    return NULL;
  }

#if CACHE_LINE_SIZE
  // Ensure driver page size is a multiple of the CPU cache line size.
  if (ftl_cfg->page_size % CACHE_LINE_SIZE) {
    ftl_cfg->logger.error("Invalid Arguments. Page size is not a multiple of the cache line size.");
    FsError2(FTL_CFG_ERR, EINVAL);
    return NULL;
  }
#endif

  // Ensure physical page size is a multiple of 512 bytes and
  // not bigger than the device block size.
  if (ftl_cfg->page_size % 512 || ftl_cfg->page_size == 0 ||
      ftl_cfg->page_size > ftl_cfg->block_size) {
    ftl_cfg->logger.error(
        "Invalid Arguments. Page size must a multiple of 512 and not bigger than the device block "
        "size.");
    FsError2(FTL_CFG_ERR, EINVAL);
    return NULL;
  }

  // Allocate memory for FTL control block. Return NULL if unable.
  ftl = FsCalloc(1, sizeof(struct ftln));
  if (ftl == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    FsError2(FTL_ENOMEM, ENOMEM);
    return NULL;
  }
#if FTLN_DEBUG_PTR
  Ftln = ftl;
#endif

  // Initialize the FTL control block.
  ftl->num_blks = ftl_cfg->num_blocks;
  ftl->page_size = ftl_cfg->page_size;
  ftl->eb_size = ftl_cfg->eb_size;
  ftl->block_size = ftl_cfg->block_size;
  ftl->pgs_per_blk = ftl->block_size / ftl->page_size;
  ftl->num_pages = ftl->pgs_per_blk * ftl->num_blks;
  ftl->start_pn = ftl_cfg->start_page;
  ftl->ndm = ftl_cfg->ndm;
  ftl->flags = ftl_cfg->flags;
  ftl->logger = ftl_cfg->logger;
  strcpy(ftl->vol_name, xfs->name);

  // Ensure pages per block doesn't exceed allotted metadata field width.
  if (ftl->pgs_per_blk > PGS_PER_BLK_MAX) {
    ftl->logger.error("Pages per block exceed maximum allowed. Expected at most %u, found %u.",
                      PGS_PER_BLK_MAX, ftl->pgs_per_blk);
    FsError2(FTL_CFG_ERR, EINVAL);
    goto FtlnAddV_err;
  }

#if !FTLN_LEGACY && FTLN_3B_PN
  // Verify number of pages doesn't exceed 3B field width.
  if (ftl->num_pages > 0x1000000) {
    ftl->logger.error("Pages exceed maximum allowed. Expected at most %u, found %u.", 0x10000000,
                      ftl->num_pages);
    FsError2(FTL_CFG_ERR, EINVAL);
    goto FtlnAddV_err;
  }
#endif

  // Compute how many volume pages are mapped by a single map page.
  ftl->mappings_per_mpg = ftl->page_size / FTLN_PN_SZ;

#if !FTLN_LEGACY
  // Determine largest possible number of volume blocks.
  for (vol_blks = ftl->num_blks - FTLN_MIN_FREE_BLKS - 1;; --vol_blks) {
    // Determine number of map pages for given number of vol blocks.
    n = (vol_blks * ftl->pgs_per_blk + ftl->mappings_per_mpg - 1) / ftl->mappings_per_mpg;
    n = n + 1;  // plus one for metapage

    // Convert to number of map blocks.
    n = (n * ftl->page_size + ftl->block_size - 1) / ftl->block_size;

#if INC_FTL_NDM_MLC
    // For MLC, double the required number of map blocks because only
    // half their pages are used.
    n *= 2;
#endif

    // Break if this number of volume blocks fits into the partition.
    if (vol_blks + n + FTLN_MIN_FREE_BLKS <= ftl->num_blks)
      break;
  }

#if INC_FTL_NDM_MLC
  // For MLC, remove another 5% of volume space to account for having
  // to advance the free volume pointer to skip possible page pair
  // corruption anytime FTL metadata is synched to flash.
  vol_blks = 95 * vol_blks / 100;
#endif

#else  // FTLN_LEGACY

// Compute the number of volume blocks using:
//                 VB = TB - MB - 4        (1)
// where VB = # volume blocks, TB = # total blocks, MB = # map blocks
// and:
//            MB = (4 * VB * PB + 1) / BS  (2)
// where PB = # pages per block and BS = block size in bytes
// Combining (1) and (2) yields (with rounding):
//   VB = TB - (4 * (TP + PB) + 6 * BS - 2) / (BS + 4 * PB)
// For MLC devices, double the number of map blocks in computation,
// because we only use half the pages in MLC map blocks.
#if INC_FTL_NDM_SLC
  vol_blks = ftl->num_blks - (4 * (ftl->num_pages + ftl->pgs_per_blk) + 6 * ftl->block_size - 2) /
                                 (ftl->block_size + 4 * ftl->pgs_per_blk);
#else
  vol_blks = ftl->num_blks - (8 * (ftl->num_pages + ftl->pgs_per_blk) + 5 * ftl->block_size + 1) /
                                 (ftl->block_size + 8 * ftl->pgs_per_blk);

  // Remove another 5% from volume space to account for the fact
  // that the free volume pointer must be advanced to skip page
  // pair corruption anytime the FTL meta information is flushed.
  vol_blks = 95 * vol_blks / 100;
#endif
#endif  // FTLN_LEGACY

  // Compute number of volume pages and subtract extra free percentage.
  // If driver specifies an acceptable amount, use it. Otherwise, use
  // 2%. Increasing number of map pages makes recycles more efficient
  // because the ratio of used to dirty pages is lower in map blocks.
  ftl->num_vpages = vol_blks * ftl->pgs_per_blk;
  n = ftl_cfg->extra_free;
  if (FLAG_IS_CLR(ftl_cfg->flags, FSF_EXTRA_FREE) || n < 2 || n > 50)
    n = 2;
  n = (n * ftl->num_vpages) / 100;
  if (n == 0)
    n = 1;
  ftl->num_vpages -= n;

#if INC_FTL_NDM_MLC
  // For MLC devices, account for the fact that the last recycled
  // volume block cannot be fully used. To be safe, assume worst case
  // scenario for max pair offset - half a block.
  ftl->num_vpages -= ftl->pgs_per_blk / 2;
#endif

  // Compute number of map pages based on number of volume pages.
  ftl->num_map_pgs = 1 + (ftl->num_vpages + ftl->mappings_per_mpg - 1) / ftl->mappings_per_mpg;
  PfAssert(ftl->num_vpages / ftl->mappings_per_mpg < ftl->num_map_pgs);

#if FTLN_DEBUG > 1
  ftl->logger.debug("Volume Pages = %u. FTL pages = %u. %u%% usage", ftl->num_vpages,
                    ftl->num_pages, (ftl->num_vpages * 100) / ftl->num_pages);
#endif

  // Allocate one or two main data pages and spare buffers. Max spare
  // use is one block worth of spare areas for multi-page writes.
  n = ftl->page_size + ftl->eb_size * ftl->pgs_per_blk;
  buf = FsAalloc(n);
  if (buf == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    FsError2(FTL_ENOMEM, ENOMEM);
    goto FtlnAddV_err;
  }
  ftl->main_buf = buf;
  ftl->spare_buf = buf + ftl->page_size;

  // Allocate memory for the block data and wear count lag arrays.
  ftl->bdata = FsCalloc(ftl->num_blks, sizeof(ui32));
  if (ftl->bdata == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    FsError2(FTL_ENOMEM, ENOMEM);
    goto FtlnAddV_err;
  }
  ftl->blk_wc_lag = FsCalloc(ftl->num_blks, sizeof(ui8));
  if (ftl->blk_wc_lag == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    FsError2(FTL_ENOMEM, ENOMEM);
    goto FtlnAddV_err;
  }
  ftl->high_wc = 0;

  // Allocate memory for map pages array (holds physical page numbers).
  ftl->mpns = FsMalloc(ftl->num_map_pgs * sizeof(ui32));
  if (ftl->mpns == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    FsError2(FTL_ENOMEM, ENOMEM);
    goto FtlnAddV_err;
  }

  // For SLC devices, adjust driver cached MPNs if too big or zero.
#if INC_FTL_NDM_SLC
  if (ftl->num_map_pgs < ftl_cfg->cached_map_pages || ftl_cfg->cached_map_pages == 0)
    ftl_cfg->cached_map_pages = ftl->num_map_pgs;

#else

  // For MLC devices, cache all map pages so that no map write occurs
  // due to cache preemption.
  ftl_cfg->cached_map_pages = ftl->num_map_pgs;
#endif

  // Allocate map page cache for new volume.
  ftl->map_cache = ftlmcNew(ftl, ftl_cfg->cached_map_pages, FtlnMapWr, rd_map_pg, ftl->page_size);
  if (ftl->map_cache == NULL) {
    ftl_cfg->logger.error("Failed to allocated memory for FTL at %s:%d", __FILE_NAME__, __LINE__);
    goto FtlnAddV_err;
  }

  // Set block read wear limit.
  if (FLAG_IS_SET(ftl_cfg->flags, FSF_READ_WEAR_LIMIT)) {
    ftl->max_rc = ftl_cfg->read_wear_limit;
  } else {
#if INC_FTL_NDM_SLC
    ftl->max_rc = SLC_NAND_RC_LIMIT;
#else
    ftl->max_rc = MLC_NAND_RC_LIMIT;
#endif
  }
  if (ftl->max_rc > RC_MASK) {
    ftl->logger.error(
        "Maximum read count in volume, exceeds max supported value. Expected %u found %u.", RC_MASK,
        ftl->max_rc);
    FsError2(FTL_CFG_ERR, EINVAL);
    goto FtlnAddV_err;
  }

  // Initialize volume state.
  FtlnStateRst(ftl);

  // Initialize the NAND FTL.
  if (init_ftln(ftl))
    goto FtlnAddV_err;

  // For recycle limit, get sum, average, and max of wear count lag.
  ftl->wear_data.cur_max_lag = ftl->wc_lag_sum = 0;
  for (n = 0; n < ftl->num_blks; ++n) {
    ui32 wc_lag = ftl->blk_wc_lag[n];

    ftl->wc_lag_sum += wc_lag;
    if (ftl->wear_data.cur_max_lag < wc_lag)
      ftl->wear_data.cur_max_lag = wc_lag;
  }
  ftl->wear_data.lft_max_lag = ftl->wear_data.cur_max_lag;
  ftl->wear_data.avg_wc_lag = ftl->wc_lag_sum / ftl->num_blks;

#if FTLN_DEBUG > 1
  // Display FTL statistics.
  FtlnStats(ftl);
#endif

  // Initialize FTL interface structure.
  xfs->num_pages = ftl->num_vpages;
  xfs->page_size = ftl->page_size;
  xfs->write_pages = FtlnWrPages;
  xfs->read_pages = FtlnRdPages;
  xfs->report = FtlnReport;
  xfs->vol = ftl;

#if FTLN_DEBUG > 1
  ftl->logger.debug("[XFS] num_pages       = %u\n\n", xfs->num_pages);
#endif

  // Register FTL volume with user.
  if (XfsAddVol(xfs))
    goto FtlnAddV_err;

  // Add FTL to FTL-NDM volume list while holding access semaphore.
  semPend(FileSysSem, WAIT_FOREVER);
  CIRC_LIST_APPEND(&ftl->link, &FtlnVols);
  semPostBin(FileSysSem);

  // Return pointer to FTL control block.
  return ftl;

// Error exit.
FtlnAddV_err:
  free_ftl(ftl);
  return NULL;
}

//  FtlnDelVol: Delete an existing FTL NDM volume (both FTL and FS)
//
//       Input: ftl = pointer to FTL control block
//
//        Note: Called with exclusive file system access held
//
//     Returns: 0 if success, -1 if error
//
int FtlnDelVol(FTLN ftl) {
  ftl->logger.debug("Deleting FTL volume.");
  // Remove volume from list of volumes.
  CIRC_NODE_REMOVE(&ftl->link);
  // Delete FTL and return success.
  free_ftl(ftl);
  return 0;
}

// FtlNdmDelVol: Delete an existing FTL NDM volume
//
//       Input: name = name of volume to delete
//
//     Returns: 0 on success, -1 on failure
//
//        Note: Called with FileSysSem semaphore held
//
int FtlNdmDelVol(const char* name) {
  CircLink* circ;

  // Acquire global file system semaphore.
  semPend(FileSysSem, WAIT_FOREVER);

  // Search all TargetFTL-NDM volume for name match.
  for (circ = CIRC_LIST_HEAD(&FtlnVols);; circ = circ->next_bck) {
    FTLN ftln;

    // Stop when end of list is reached.
    if (CIRC_LIST_AT_END(circ, &FtlnVols)) {
      // Release file system exclusive access.
      semPostBin(FileSysSem);

      // Volume not found, assign errno and return -1.
      return FsError2(FTL_NOT_FOUND, ENOENT);
    }

    // If volume found, delete it and return success.
    ftln = (FTLN)circ;
    if (strcmp(ftln->vol_name, name) == 0) {
      int rc;

      // Delete this TargetFTL-NDM volume.
      rc = FtlnDelVol(ftln);

      // Release file system exclusive access and return status.
      semPostBin(FileSysSem);
      return rc;
    }
  }
}
