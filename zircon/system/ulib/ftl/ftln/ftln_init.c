// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftlnp.h"

#if INC_FTL_NDM
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
    ui32 b, wc, *lp = (ui32 *)(ftl->main_buf + FTLN_META_DATA_BEG);

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
            if (NUM_USED(ftl->bdata[b]) || IS_MAP_BLK(ftl->bdata[b]))
                return NDM_PAGE_INVALID;

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
    ui32 mpn, n, *ppns = (ui32 *)ftl->main_buf;
    int status;

    // Call driver validity check. Return -1 if error.
    ++ftl->stats.page_check;
    status = ftl->page_check(apn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
    if (status < 0)
        return FtlnFatErr(ftl);

    // If page is erased or invalid, return its status.
    if (status != NDM_PAGE_VALID)
        return status;

    // If MPN too big, page is invalid.
    mpn = GET_SA_VPN(ftl->spare_buf);
    if (mpn >= ftl->num_map_pgs)
        return NDM_PAGE_INVALID;

    // If meta-page, check version, type, and format. Process if enabled.
    if (mpn == ftl->num_map_pgs - 1) {
        ui32 type, vers = RD32_LE(&ppns[0]);

        // Check if first metapage version number.
        if (vers == FTLN_META_VER0) {
            ui32 b, i;

            // If recycle block wrong, page is invalid.
            b = RD32_LE(&ppns[1]);
            if (b >= ftl->num_blks && b != (ui32)-1)
                return NDM_PAGE_INVALID;

            // Rest of the page should be erased. If not, page is invalid.
            for (i = 2; i < ftl->page_size / sizeof(ui32); ++i)
                if (RD32_LE(&ppns[i]) != (ui32)-1)
                    return NDM_PAGE_INVALID;
        }

        // Else check if second metapage version.
        else if (vers == FTLN_META_VER1) {
            // Read the meta-page type.
            type = RD32_LE(&ppns[1]);

            // Check if 'continue format' metadata.
            if (type == CONT_FORMAT) {
                // Rest of meta-page should be erased.
                for (n = 2; n < ftl->page_size / sizeof(ui32); ++n)
                    if (RD32_LE(&ppns[n]) != (ui32)-1)
                        return NDM_PAGE_INVALID;

                // If enabled, resume the format.
                if (process)
                    if (FtlnFormat(ftl, (apn - ftl->start_pn) / ftl->pgs_per_blk))
                        return -1;
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
                        //---------------------------------------------------------
                        // Verify and apply elist page. Return if page invalid.
                        //---------------------------------------------------------
                        status = proc_elist(ftl);
                        if (status != NDM_PAGE_VALID)
                            return status;

                        //---------------------------------------------------------
                        // If first (perhaps only) page was processed, finished!
                        //---------------------------------------------------------
                        if (apn == ap0)
                            break;

//---------------------------------------------------------
// Move to next written page in backwards direction. If
// MLC flash, move to page whose pair has higher offset.
//---------------------------------------------------------
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
                        if (ftl->type == NDM_MLC)
#endif
#if INC_NDM_MLC
                            for (;;) {
                                ui32 pg_offset = --apn % ftl->pgs_per_blk;

                                if (pg_offset == 0)
                                    break;
                                if (ftl->pair_offset(pg_offset, ftl->ndm) >= pg_offset)
                                    break;
                            }
#endif
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
                        else
#endif
#if INC_FTL_NDM_SLC || INC_FTL_NOR_WR1
                            --apn;
#endif

                        //---------------------------------------------------------
                        // Call driver to read/check next page. Return -1 if error.
                        //---------------------------------------------------------
                        ++ftl->stats.page_check;
                        status = ftl->page_check(apn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
                        if (status < 0)
                            return FtlnFatErr(ftl);

                        //---------------------------------------------------------
                        // If page is erased or invalid, return its status.
                        //---------------------------------------------------------
                        if (status != NDM_PAGE_VALID)
                            return status;

                        //---------------------------------------------------------
                        // Verify the metadata version is correct.
                        //---------------------------------------------------------
                        if (RD32_LE(&ppns[0]) != FTLN_META_VER1)
                            return NDM_PAGE_INVALID;

                        //---------------------------------------------------------
                        // Verify the metadata type is correct.
                        //---------------------------------------------------------
                        if (RD32_LE(&ppns[1]) != ERASED_LIST)
                            return NDM_PAGE_INVALID;
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
            if (pn >= ftl->num_pages && pn != UNMAPPED_PN)
                return NDM_PAGE_INVALID;
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
    ui32 b, bc, *bcs, mpn, n, pn, po, *b_ptr;

    // Allocate space to hold block count for each map page array entry.
    bcs = FsCalloc(ftl->num_map_pgs, sizeof(ui32));
    if (bcs == NULL)
        return -1;

    // Loop over every block looking for map blocks. This list was made
    // by format_status() and only has one with the highest BC, but may
    // include old map blocks that didn't get erased after their recycle.
    for (b = 0; b < ftl->num_blks; ++b) {
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
            if (ftl->type == NDM_MLC && ftl->pair_offset(po, ftl->ndm) < po)
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
                status = ftl->read_spare(pn, ftl->spare_buf, ftl->ndm);

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
            if (po == 0)
                bc = GET_SA_BC(ftl->spare_buf);
            else if (bc != GET_SA_BC(ftl->spare_buf)) {
#if FTLN_DEBUG > 1
                printf("build_ma: b = %u, po = %u, i_bc = %u vs 0_bc = %u\n", b, po,
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
                printf("build_ma: b = %u, po = %u, mpn = %u, max = %u\n", b, po, mpn,
                       ftl->num_map_pgs);
#endif

                // Should not be, but page is invalid. Break to skip block.
                break;
            }

            // If no entry for this MPN in array OR entry in same block as
            // current block OR entry in a block with a lower block count,
            // update array entry with current page.
            if (ftl->mpns[mpn] == (ui32)-1 || ftl->mpns[mpn] / ftl->pgs_per_blk == b ||
                bcs[mpn] < bc) {
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
                printf("build_ma: mpn = %u, old_pn = %d, new_pn = %u\n", mpn, ftl->mpns[mpn],
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
        printf("  -> MPN[%2u] = %u\n", mpn, pn);
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
            PfAssert(!IS_FREE(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b]));
            if (IS_FREE(ftl->bdata[b]) || IS_MAP_BLK(ftl->bdata[b]))
                return -1;

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
    printf("vol block %d has lowest used page offset (%d)\n", ftl->resume_vblk, ftl->resume_po);
#endif

    // Clean temporary use of vol block read-wear field for page offset.
    for (b = 0; b < ftl->num_blks; ++b)
        if (NUM_USED(ftl->bdata[b]) && !IS_MAP_BLK(ftl->bdata[b]))
            ftl->bdata[b] &= ~RC_MASK;

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
#if FTLN_DEBUG
                ++ftl->max_wc_over;
#endif
            } else
                ftl->blk_wc_lag[lb] += increase;

#if FTLN_DEBUG
            // If new value, record maximum encountered wear lag.
            if (ftl->max_wc_lag < ftl->blk_wc_lag[lb])
                ftl->max_wc_lag = ftl->blk_wc_lag[lb];
#endif
        }

        // Remember new high wear count.
        ftl->high_wc = wc;
    }

    // Set block wear count lag, avoiding ui8 overflow.
    if (ftl->high_wc - wc > 0xFF) {
        ftl->blk_wc_lag[b] = 0xFF;
#if FTLN_DEBUG
        ++ftl->max_wc_over;
#endif
    } else
        ftl->blk_wc_lag[b] = ftl->high_wc - wc;

#if FTLN_DEBUG
    // If new value, record maximum encountered wear lag.
    if (ftl->max_wc_lag < ftl->blk_wc_lag[b])
        ftl->max_wc_lag = ftl->blk_wc_lag[b];
#endif
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
        rc = ftl->read_spare(pn, ftl->spare_buf, ftl->ndm);
        if (rc == -2)
            return FtlnFatErr(ftl);

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
                        rc = ftl->read_spare(pn + n, ftl->spare_buf, ftl->ndm);
                        if (rc == -2)
                            return FtlnFatErr(ftl);

                        // If read good and counts match, set block wear count lag.
                        bc2 = GET_SA_BC(ftl->spare_buf);
                        wc2 = GET_SA_WC(ftl->spare_buf);
                        if ((rc == 0) && (bc == bc2) && (wc == wc2)) {
                            set_wc_lag(ftl, b, wc, &low_wc);
                            break;
                        }

#if INC_FTL_NDM_MLC
                        // If MLC, try first next page that has no earlier pair, in
                        // case FTL skipped volume pages for sync operation.
                        if ((ftl->type == NDM_MLC) && (n == 1)) {
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
            rc = ftl->page_check(pn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
            if (rc < 0)
                return FtlnFatErr(ftl);

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
                rc = ftl->read_spare(pn + n, ftl->spare_buf, ftl->ndm);
                if (rc == -2)
                    return FtlnFatErr(ftl);
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
                    rc = ftl->page_check(pn + n, ftl->main_buf, ftl->spare_buf, ftl->ndm);
                    if (rc < 0)
                        return FtlnFatErr(ftl);

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

                        printf("resume_vblk=%d bdata=0x%X", vb, ftl->bdata[vb]);
                        if (IS_FREE(ftl->bdata[vb]))
                            puts(" free blk");
                        else if (IS_MAP_BLK(ftl->bdata[vb]))
                            puts(" map blk");
                        else
                            putchar('\n');
                    }
#endif

                    // Mark the resume temporary block free and break.
                    ftl->bdata[b] = FREE_BLK_FLAG;
                    ++ftl->num_free_blks;
                    break;
                }
            }

            // If copy-end not found, erase block. Return -1 if I/O error.
            if (!ftl->copy_end_found)
                if (FtlnEraseBlk(ftl, b))
                    return -1;
        }

        // Else this looks like a map block.
        else {
            // Check block's first map page for validity. Return -1 if error.
            rc = map_page_check(ftl, pn, FALSE);
            if (rc < 0)
                return -1;

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
    if (formatted == FALSE)
        return FALSE;

    // Compute the average 'high_wc' lag.
    for (avg_lag = n = b = 0; b < ftl->num_blks; ++b)
        if (GET_RC(ftl->bdata[b]) != 100) {
            avg_lag += ftl->blk_wc_lag[b];
            ++n;
        }
    if (n)
        avg_lag = (avg_lag + n / 2) / n;

    // Apply average wear offset to every block marked as needing it.
    for (b = 0; b < ftl->num_blks; ++b)
        if ((ftl->bdata[b] & RC_MASK) == 100) {
            ftl->bdata[b] &= ~RC_MASK;
            ftl->blk_wc_lag[b] = avg_lag;
        }

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

#if INC_FAT_MBR
//    read_bpb: Read the FAT boot sector to set frst_data_sect
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 for success or no boot sector, -1 on error
//
static int read_bpb(FTLN ftl) {
    ui32 pn;
    FATPartition part;

    // Prepare to (potentially) write one map page.
    if (FtlnRecCheck(ftl, -1))
        return -1;

    // Retrieve physical page number for sector 0. Return -1 if error.
    if (FtlnMapGetPpn(ftl, 0, &pn) < 0)
        return -1;

    // Return 0 if boot sector is unmapped.
    if (pn == (ui32)-1)
        return 0;

    // Read sector 0. Return -1 if error.
    if (FtlnRdPage(ftl, pn, ftl->main_buf))
        return -1;

    // If one valid partition, use it to read location of boot sector.
    if (FatGetPartitions(ftl->main_buf, &part, 1) == 1) {
        ftl->vol_frst_sect = part.first_sect;
        ftl->num_vsects -= ftl->vol_frst_sect;
    }

    // Read boot sector into temporary buffer. Return -1 if error.
    if (FtlnRdSects(ftl->main_buf, ftl->vol_frst_sect, 1, ftl))
        return -1;

    // Extract frst_clust_sect from the boot information. Return status.
    return FtlnSetClustSect1(ftl, ftl->main_buf, TRUE);
}
#endif // INC_FAT_MBR

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
    if (map_page_check(ftl, ftl->start_pn + pn, TRUE) < 0)
        return -1;

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
    return ftl->write_page(pn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
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
    ui32 po, vpn;
    ui32 src_pg0 = ftl->start_pn + src_b * ftl->pgs_per_blk;
    ui32 dst_pg0 = ftl->start_pn + dst_b * ftl->pgs_per_blk;
    ui32 wc = ftl->high_wc - ftl->blk_wc_lag[src_b];

    // Copy all used pages from selected volume block to free block.
    for (po = 0; po <= ftl->resume_po; ++po) {
        // Read source page's spare area.
        ++ftl->stats.read_spare;
        rc = ftl->read_spare(src_pg0 + po, ftl->spare_buf, ftl->ndm);

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
        if (ftl->xfer_page(src_pg0 + po, dst_pg0 + po, ftl->main_buf, ftl->spare_buf, ftl->ndm))
            return FtlnFatErr(ftl);
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
    if (formatted < 0)
        return -1;

    // If unformatted, blocks are free w/zero 'high_wc' lag.
    if (formatted == FALSE) {
        for (b = 0; b < ftl->num_blks; ++b) {
            ftl->blk_wc_lag[b] = 0;
            ftl->bdata[b] = FREE_BLK_FLAG;
        }
        ftl->num_free_blks = ftl->num_blks;
        ftl->high_bc = 1;  // initial block count of unformatted volumes
        return 0;
    }

    // Look for all the valid map pages on all the map blocks.
    if (build_map(ftl))
        return -1;

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
    if (meta_read(ftl) < 0)
        return -1;

    // Erase unused map blocks. Return -1 if error
    for (b = 0; b < ftl->num_blks; ++b)
        if (IS_MAP_BLK(ftl->bdata[b]) && (NUM_USED(ftl->bdata[b]) == 0))
            if (FtlnEraseBlk(ftl, b))
                return -1;

    // If free block count is below reserved number, a recycle has been
    // interrupted by a power failure. Must avoid losing additional free
    // blocks from additional power failures. Resume restores the free
    // map and volume page lists by copying valid entries to an erased
    // block, ensuring they don't have undetectable corruption from an
    // interrupted page write or block erase command. If resume is
    // interrupted by a power failure, no free blocks are lost.
    if (ftl->num_free_blks < FTLN_MIN_FREE_BLKS) {
#if DEBUG_RESUME
        printf("Resuming: %u free blocks\n", ftl->num_free_blks);
        printf("map block %u has used page offset of %u/%u\n", ftl->high_bc_mblk,
               ftl->high_bc_mblk_po, ftl->pgs_per_blk);
        printf("vol block %u has used page offset of %u/%u\n", ftl->resume_vblk, ftl->resume_po,
               ftl->pgs_per_blk);
#endif

        // Resume needs one free block and should have it.
        PfAssert(ftl->num_free_blks >= 1);
        if (ftl->num_free_blks < 1)
            return -1;

        // Check if low page-offset volume block has unused pages.
        if (ftl->resume_po < ftl->pgs_per_blk - 1) {
#ifdef FTL_RESUME_STRESS
            ++FtlVblkResumeCnt;
#endif

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
                if (b == (ui32)-1)
                    return b;

                // If the block is unerased, erase it now. Return -1 if error.
                if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
                    if (FtlnEraseBlk(ftl, b))
                        return (ui32)-1;

                // Decrement free block count.
                --ftl->num_free_blks;

                // Copy used pages to temp block.
                if (resume_copy(ftl, ftl->resume_vblk, b, COPY_BLK_MARK))
                    return -1;

                // Write "end of copy" mark on next temp block page.
                if (copy_end_mark(ftl, b))
                    return -1;
            }

            // Erase the volume block with the lowest used page-offset.
            if (FtlnEraseBlk(ftl, ftl->resume_vblk))
                return -1;

            // Copy the temp block's contents back to the volume block.
            if (resume_copy(ftl, b, ftl->resume_vblk, 0xFFFFFFFF))
                return -1;

            // Mark resumed block as a volume block with 'n' used pages.
            ftl->bdata[ftl->resume_vblk] = n << 20;  // clr free & erased flags

            // Erase the temp copy block.
            if (FtlnEraseBlk(ftl, b))
                return -1;

            // Assign the resumed ftl->free_vpn value.
            ftl->free_vpn = ftl->resume_vblk * ftl->pgs_per_blk + ftl->resume_po + 1;
        }

        // Check if high-block-count map block has unused pages.
        if (ftl->high_bc_mblk_po < ftl->pgs_per_blk - 1) {
#ifdef FTL_RESUME_STRESS
            ++FtlMblkResumeCnt;
#endif

            // Find free block with lowest wear count. Error if none free.
            b = FtlnLoWcFreeBlk(ftl);
            if (b == (ui32)-1)
                return b;

            // If the block is unerased, erase it now. Return -1 if error.
            if ((ftl->bdata[b] & ERASED_BLK_FLAG) == 0)
                if (FtlnEraseBlk(ftl, b))
                    return (ui32)-1;

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
            if (FtlnRecycleMapBlk(ftl, ftl->high_bc_mblk))
                return -1;
        }
    }

#if INC_FAT_MBR
    // For FAT volumes, read in boot sector, if it exists, to set
    // frst_clust_sect. Return -1 if error.
    if (FLAG_IS_SET(ftl->flags, FTLN_FAT_VOL))
        if (read_bpb(ftl))
            return -1;
#endif

#if FTLN_DEBUG > 1
    printf("init_ftln: FTL formatted - hi_bc = %u, hi_wc = %u\n", ftl->high_bc, ftl->high_wc);
#endif

    // Do recycles if needed and return status.
    return FtlnRecCheck(ftl, 0);
}

//    free_ftl: Callback used inside FtlnFreeFTL()
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
#if INC_FTL_PAGE_CACHE
    if (ftl->vol_cache)
        ftlvcDelete(ftl->vol_cache);
#endif
    FsFree(ftl);
}

// Global Function Definitions

//  FtlnAddVol: Add a new TargetFTL-NDM volume
//
//      Inputs: ftl_dvr = pointer to FTL NDM driver control block
//              ftl_type = FTL type (FTLN_XFS_VOL or FTLN_FAT_VOL)
//              sect_size = FAT or XFS sector size
//              fs_vol = pointer to FS driver's volume control block
//
//     Returns: Newly created FTL handle on success, NULL on error
//
void* FtlnAddVol(FtlNdmVol* ftl_dvr, int ftl_type, int sect_size, void* fs_vol) {
    ui32 n, vol_blks;
    ui8* buf;
    FTLN ftl;

    // Ensure shared FTL vstat fields remain at same offset.
    PfAssert(offsetof(vstat_fat, garbage_level) == offsetof(vstat_xfs, garbage_level));
    PfAssert(offsetof(vstat_fat, ftl_type) == offsetof(vstat_xfs, ftl_type));

    // If number of blocks less than 7, FTL-NDM cannot work.
    if (ftl_dvr->num_blocks < 7) {
        FsError(EINVAL);
        return NULL;
    }

#if CACHE_LINE_SIZE
    // Ensure driver page size is a multiple of the CPU cache line size.
    if (ftl_dvr->page_size % CACHE_LINE_SIZE) {
        FsError(EINVAL);
        return NULL;
    }
#endif

    // Ensure physical page size is a multiple of FAT sector size and
    // not bigger than the device block size.
    if (ftl_dvr->page_size % FAT_SECT_SZ || ftl_dvr->page_size == 0 ||
        ftl_dvr->page_size > ftl_dvr->block_size) {
        FsError(EINVAL);
        return NULL;
    }

#if OS_PARM_CHECK
    // Ensure FTL flags are valid.
    if (ftl_dvr->flags &
        ~(FSF_EXTRA_FREE
#if INC_FTL_PAGE_CACHE
          | FSF_FTL_PAGE_CACHE
#endif
          | FSF_READ_WEAR_LIMIT)) {
        FsError(EINVAL);
        return NULL;
    }
#endif

    // Ensure driver has an NDM pointer.
    PfAssert(ftl_dvr->ndm);

    // Allocate memory for FTL control block. Return NULL if unable.
    ftl = FsCalloc(1, sizeof(struct ftln));
    if (ftl == NULL)
        return NULL;
#if FTLN_DEBUG_PTR
    Ftln = ftl;
#endif

    // Set callback to free FTL resources from generic FTL NDM layer.
    ftl->free_ftl = free_ftl;

    // Acquire exclusive access to upper file system.
    semPend(FileSysSem, WAIT_FOREVER);

    // Add volume to list of FTL volumes.
    CIRC_LIST_APPEND(&ftl->link, &FtlnVols);

    // Set all FTL driver dependent variables.
    ftl->page_size = ftl_dvr->page_size;
    ftl->eb_size = ftl_dvr->eb_size;
    ftl->block_size = ftl_dvr->block_size;
    ftl->num_blks = ftl_dvr->num_blocks;
    ftl->start_pn = ftl_dvr->start_page;
    ftl->ndm = ftl_dvr->ndm;
    ftl->type = ftl_dvr->type;
    SET_FLAG(ftl->flags, ftl_type);
    ftl->sect_size = sect_size;

    // Derive other driver dependent variables.
    ftl->pgs_per_blk = ftl->block_size / ftl->page_size;
    if (ftl->pgs_per_blk > PGS_PER_BLK_MAX) {
        FsError(EINVAL);
        semPostBin(FileSysSem);
        goto FtlnAddV_err;
    }
    ftl->num_pages = ftl->pgs_per_blk * ftl->num_blks;
#if !FTLN_LEGACY
#if FTLN_3B_PN
    if (ftl->num_pages > 0x1000000) {
        FsError(EFBIG);
        semPostBin(FileSysSem);
        goto FtlnAddV_err;
    }
#endif
#endif // !FTLN_LEGACY
    ftl->sects_per_page = ftl->page_size / ftl->sect_size;

    // Release file system exclusive access.
    semPostBin(FileSysSem);

    // Copy the driver callback functions.
    ftl->write_page = ftl_dvr->write_data_and_spare;
    ftl->write_pages = ftl_dvr->write_pages;
    ftl->read_spare = ftl_dvr->read_spare;
    ftl->read_pages = ftl_dvr->read_pages;
    ftl->page_check = ftl_dvr->page_check;
    ftl->xfer_page = ftl_dvr->transfer_page;
    ftl->erase_block = ftl_dvr->erase_block;
#if INC_FTL_NDM_MLC
    ftl->pair_offset = ftl_dvr->pair_offset;
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
        // If MLC, double the required number of map blocks because only
        // half their pages are used.
        if (ftl->type == NDM_MLC)
            n *= 2;
#endif

        // Break if this number of volume blocks fits into the partition.
        if (vol_blks + n + FTLN_MIN_FREE_BLKS <= ftl->num_blks)
            break;
    }

#if INC_FTL_NDM_MLC
    // If MLC, remove another 5% of volume space to account for having
    // to advance the free volume pointer to skip possible page pair
    // corruption anytime FTL metadata is synched to flash.
    if (ftl->type == NDM_MLC)
        vol_blks = 95 * vol_blks / 100;
#endif

#else // FTLN_LEGACY

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
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
    if (ftl->type == NDM_SLC)
#endif
#if INC_FTL_NDM_SLC || INC_FTL_NOR_WR1
    {
        vol_blks = ftl->num_blks -
                   (4 * (ftl->num_pages + ftl->pgs_per_blk) + 6 * ftl->block_size - 2) /
                       (ftl->block_size + 4 * ftl->pgs_per_blk);
    }
#endif
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
    else
#endif
#if INC_FTL_NDM_MLC
    {
        vol_blks = ftl->num_blks -
                   (8 * (ftl->num_pages + ftl->pgs_per_blk) + 5 * ftl->block_size + 1) /
                       (ftl->block_size + 8 * ftl->pgs_per_blk);

        // Remove another 5% from volume space to account for the fact
        // that the free volume pointer must be advanced to skip page
        // pair corruption anytime the FTL meta information is flushed.
        vol_blks = 95 * vol_blks / 100;
    }
#endif
#endif // FTLN_LEGACY

    // Compute number of volume pages and subtract extra free percentage.
    // If driver specifies an acceptable amount, use it. Otherwise, use
    // 2%. Increasing number of map pages makes recycles more efficient
    // because the ratio of used to dirty pages is lower in map blocks.
    ftl->num_vpages = vol_blks * ftl->pgs_per_blk;
    n = ftl_dvr->extra_free;
    if (FLAG_IS_CLR(ftl_dvr->flags, FSF_EXTRA_FREE) || n < 2 || n > 50)
        n = 2;
    n = (n * ftl->num_vpages) / 100;
    if (n == 0)
        n = 1;
    ftl->num_vpages -= n;

#if INC_FTL_NDM_MLC
    // For MLC devices, account for the fact that the last recycled
    // volume block cannot be fully used. To be safe, assume worst case
    // scenario for max pair offset - half a block.
    if (ftl->type == NDM_MLC)
        ftl->num_vpages -= ftl->pgs_per_blk / 2;
#endif

    // Compute number of map pages based on number of volume pages.
    ftl->num_map_pgs = 1 + (ftl->num_vpages + ftl->mappings_per_mpg - 1) / ftl->mappings_per_mpg;
    PfAssert(ftl->num_vpages / ftl->mappings_per_mpg < ftl->num_map_pgs);

#if FTLN_DEBUG > 1
    printf("\nVOL PAGES = %u, FTL PAGES = %u, %u%% usage\n", ftl->num_vpages, ftl->num_pages,
           (ftl->num_vpages * 100) / ftl->num_pages);
#endif

    // Set the number of sectors based on the number of pages.
    ftl->num_vsects = ftl->num_vpages * ftl->sects_per_page;

// Allocate one or two main data pages and spare buffers. Max spare
// use is one block worth of spare areas for multi-page writes.
#if INC_SECT_FTL
    n = 2 * ftl->page_size + ftl->eb_size * ftl->pgs_per_blk;
#else
    n = 1 * ftl->page_size + ftl->eb_size * ftl->pgs_per_blk;
#endif
    buf = FsAalloc(n);
    if (buf == NULL)
        goto FtlnAddV_err;
    ftl->main_buf = buf;
    buf += ftl->page_size;
#if INC_SECT_FTL
    ftl->swap_page = buf;
    buf += ftl->page_size;
#endif
    ftl->spare_buf = buf;

    // Allocate memory for the block data and wear count lag arrays.
    ftl->bdata = FsCalloc(ftl->num_blks, sizeof(ui32));
    if (ftl->bdata == NULL)
        goto FtlnAddV_err;
    ftl->blk_wc_lag = FsCalloc(ftl->num_blks, sizeof(ui8));
    if (ftl->blk_wc_lag == NULL)
        goto FtlnAddV_err;
    ftl->high_wc = 0;

    // Allocate memory for map pages array (holds physical page numbers).
    ftl->mpns = FsMalloc(ftl->num_map_pgs * sizeof(ui32));
    if (ftl->mpns == NULL)
        goto FtlnAddV_err;

// For SLC devices, adjust driver cached MPNs if too big or zero.
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
    if (ftl->type == NDM_SLC)
#endif
#if INC_FTL_NDM_SLC || INC_FTL_NOR_WR1
    {
        if (ftl->num_map_pgs < ftl_dvr->cached_map_pages || ftl_dvr->cached_map_pages == 0)
            ftl_dvr->cached_map_pages = ftl->num_map_pgs;
    }
#endif

// For MLC devices, cache all map pages so that no map write occurs
// due to cache preemption.
#if INC_FTL_NDM_MLC && (INC_FTL_NDM_SLC || INC_FTL_NOR_WR1)
    else
#endif
#if INC_NDM_MLC
        ftl_dvr->cached_map_pages = ftl->num_map_pgs;
#endif

    // Allocate map page cache for new volume.
    ftl->map_cache = ftlmcNew(ftl, ftl_dvr->cached_map_pages, FtlnMapWr, FtlnMapRd, ftl->page_size);
    if (ftl->map_cache == NULL)
        goto FtlnAddV_err;

#if INC_FTL_PAGE_CACHE
    // If FAT volume and driver requests it, allocate volume page cache.
    if (FLAG_IS_SET(ftl->flags, FTLN_FAT_VOL) && FLAG_IS_SET(ftl_dvr->flags, FSF_FTL_PAGE_CACHE)) {
        ftl->vol_cache =
            ftlvcNew(ftl, ftl_dvr->cached_vol_pages, FtlnVpnWr, FtlnVpnRd, ftl->page_size);
        if (ftl->vol_cache == NULL)
            goto FtlnAddV_err;
    }
#endif

    // Set block read wear limit and mark no block at limit.
    if (FLAG_IS_SET(ftl_dvr->flags, FSF_READ_WEAR_LIMIT))
        ftl->max_rc = ftl_dvr->read_wear_limit;
#if INC_FTL_NDM_MLC
    else
#if INC_FTL_NDM_SLC || INC_FTL_NOR_WR1
        if (ftl_dvr->type == NDM_MLC)
#endif
        ftl->max_rc = MLC_NAND_RC_LIMIT;
#endif
#if INC_FTL_NDM_SLC
    else
#if INC_FTL_NOR_WR1
        if (ftl_dvr->type == NDM_SLC)
#endif
        ftl->max_rc = SLC_NAND_RC_LIMIT;
#endif
#if INC_FTL_NOR_WR1
    else
        ftl->max_rc = NOR_RC_LIMIT;
#endif
    if (ftl->max_rc > RC_MASK) {
        FsError(EINVAL);
        goto FtlnAddV_err;
    }

    // Initialize volume state.
    FtlnStateRst(ftl);

    // Initialize the NAND FTL.
    if (init_ftln(ftl))
        goto FtlnAddV_err;

#if FTLN_DEBUG > 1
    // Display FTL statistics.
    FtlnStats(ftl);
#endif

#if INC_SECT_FTL
    // If FAT volume, prepare for registration with TargetFAT.
    if (ftl_type == FTLN_FAT_VOL) {
        FatVol* fat = (FatVol*)fs_vol;

// Initialize TargetFAT driver structure.
#if INC_FAT_MBR
        fat->start_sect = ftl->vol_frst_sect;
        fat->num_sects = ftl->num_vsects - ftl->vol_frst_sect;
#else
        fat->start_sect = 0;
        fat->num_sects = ftl->num_vsects;
#endif
        fat->vol = ftl;
        fat->write_sectors = FtlnWrSects;
        fat->read_sectors = FtlnRdSects;
        fat->report = FtlnReport;
        fat->flags |= FSF_BLUNK_FTL;

#if FAT_DEBUG
        printf("\nFAT STATS:\n");
        printf("  - start_sect      = %u\n", fat->start_sect);
        printf("  - num_sects       = %u\n", fat->num_sects);
        printf("  - min_clust_size  = %u\n", fat->min_clust_size);
        printf("  - flags           = 0x%X\n\n", fat->flags);
#endif

        // Standard FTL-FAT settings.
        fat->fixed = TRUE;
        fat->num_heads = FAT_NUM_HEADS;
        fat->sects_per_trk = FAT_SECTS_PER_TRACK;

        // Save volume name.
        strcpy(ftl->vol_name, fat->name);
    }
#endif // INC_SECT_FTL

#if INC_PAGE_FTL
    // If XFS volume, prepare for registration with TargetXFS.
    if (ftl_type == FTLN_XFS_VOL) {
        XfsVol* xfs = (XfsVol*)fs_vol;

        // Initialize TargetXFS driver structure with FTL specific fields.
        xfs->start_page = 0;
        xfs->num_pages = ftl->num_vsects;
        xfs->page_size = ftl->page_size;
        xfs->vol = ftl;
        xfs->write_pages = FtlnWrSects;
        xfs->read_pages = FtlnRdSects;
        xfs->report = FtlnReport;

#if FTLN_DEBUG > 1
        printf("\nXFS STATS:\n");
        printf("  - start_page      = %u\n", xfs->start_page);
        printf("  - num_pages       = %u\n\n", xfs->num_pages);
#endif

        // Save volume name.
        strcpy(ftl->vol_name, xfs->name);
    }
#endif // INC_PAGE_FTL

    // Return pointer to FTL control block.
    return ftl;

// Error exit.
FtlnAddV_err:
    semPend(FileSysSem, WAIT_FOREVER);
    CIRC_NODE_REMOVE(&ftl->link);
    free_ftl(ftl);
    semPostBin(FileSysSem);
    return NULL;
}

#if INC_SECT_FTL
// FtlNdmAddFatFTL: Create a new TargetFTL-NDM FAT FTL
//
//      Inputs: ftl_dvr = pointer to FTL NDM driver control block
//              fat = pointer to FAT pseudo driver control block
//
//     Returns: Pointer to FTL control block on success, else NULL
//
void* FtlNdmAddFatFTL(FtlNdmVol* ftl_dvr, FatVol* fat) {
    // Determine sector size.
    fat->sect_size = FatGetSectSize(fat);
    if (fat->sect_size == 0)
        return NULL;

    // Create new FTL and return FTL handle.
    return FtlnAddVol(ftl_dvr, FTLN_FAT_VOL, fat->sect_size, fat);
} //lint !e818
#endif // INC_SECT_FTL

#if INC_PAGE_FTL
// FtlNdmAddXfsFTL: Create a new TargetFTL-NDM XFS FTL
//
//      Inputs: ftl_dvr = pointer to FTL NDM driver control block
//              xfs = pointer to XFS pseudo driver control block
//
//     Returns: -1 if error, 0 for success
//
void* FtlNdmAddXfsFTL(FtlNdmVol* ftl_dvr, XfsVol* xfs) {
    // Create new FTL and return FTL handle.
    return FtlnAddVol(ftl_dvr, FTLN_XFS_VOL, ftl_dvr->page_size, xfs);
} //lint !e818
#endif // INC_PAGE_FTL

// FtlnFreeFTL: Free an existing FTL volume
//
//       Input: handle = FTL handle
//
void FtlnFreeFTL(void* handle) {
    FTLN ftln = handle;

    // Acquire exclusive access to upper file system.
    semPend(FileSysSem, WAIT_FOREVER);

    // Remove FTL from list and free its memory.
    CIRC_NODE_REMOVE(&ftln->link);
    ftln->free_ftl(ftln);

    // Release exclusive access to upper file system.
    semPostBin(FileSysSem);
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
    // Remove volume from list of volumes.
    CIRC_NODE_REMOVE(&ftl->link);

    // Release file system exclusive access.
    semPostBin(FileSysSem);

// Delete file system volume.
#if INC_PAGE_FTL && INC_SECT_FTL
    if (FLAG_IS_SET(ftl->flags, FTLN_XFS_VOL))
#endif
#if INC_PAGE_FTL
        (void)XfsDelVol(ftl->vol_name);
#endif
#if INC_PAGE_FTL && INC_SECT_FTL
    else
#endif
#if INC_SECT_FTL
        (void)FatDelVol(ftl->vol_name);
#endif

    // Acquire exclusive access to upper file system.
    semPend(FileSysSem, WAIT_FOREVER);

    // Delete FTL and return success.
    ftl->free_ftl(ftl);
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
            return FsError(ENOENT);
        }

        // If volume found, delete it and return success.
        ftln = (FTLN)circ;
        if (FNameEqu(ftln->vol_name, name)) {
            int rc;

            // Delete this TargetFTL-NDM volume.
            rc = FtlnDelVol(ftln);

            // Release file system exclusive access and return status.
            semPostBin(FileSysSem);
            return rc;
        }
    }
}
#endif // INC_FTL_NDM
