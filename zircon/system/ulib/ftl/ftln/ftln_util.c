// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftlnp.h"

#include <strings.h>

// Local Function Definitions

//  format_ftl: Erase all non-free blocks
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: 0 on success, -1 on error
//
static int format_ftl(FTLN ftl) {
    ui32 meta_block;

    // Get number of block that will hold the metapage.
    if (ftl->free_mpn == (ui32)-1)
        meta_block = FtlnLoWcFreeBlk(ftl);
    else
        meta_block = ftl->free_mpn / ftl->pgs_per_blk;

    // Write meta page, to indicate that format is in progress.
    memset(ftl->main_buf, 0xFF, ftl->page_size);
    if (FtlnMetaWr(ftl, CONT_FORMAT))
        return -1;

    // Erase all map blocks, mark all blocks free, and reset the FTL.
    return FtlnFormat(ftl, meta_block);
}

// set_high_wc: Set highest wear count and adjust wear offsets
//
//      Inputs: ftl = pointer to FTL control block
//              high_b = block with new highest wear count
//              high_b_wc = new highest wear count
//
static void set_high_wc(FTLN ftl, ui32 high_b, ui32 high_b_wc) {
    ui32 b;

    // Highest wear count should only go up by one and new highest block
    // should have contained highest wear (0 'high_wc' lag) before.
    PfAssert(ftl->high_wc + 1 == high_b_wc && ftl->blk_wc_lag[high_b] == 0);

    // Loop over all other blocks adjusting their 'high_wc' lags.
    for (b = 0; b < ftl->num_blks; ++b)
        if (b != high_b) {
            if (ftl->blk_wc_lag[b] < 0xFF)
                ++ftl->blk_wc_lag[b];
#if FTLN_DEBUG
            else
                ++ftl->max_wc_over;

            // If new value, record maximum encountered wear lag.
            if (ftl->max_wc_lag < ftl->blk_wc_lag[b])
                ftl->max_wc_lag = ftl->blk_wc_lag[b];
#endif
        }

    // Update highest wear count.
    ftl->high_wc = high_b_wc;
}

// first_free_blk: Find the first free block, counting from block zero
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: block number if successful, else (ui32)-1 if none free
//
static ui32 first_free_blk(CFTLN ftl) {
    ui32 b;

    // Search for first free block.
    for (b = 0;; ++b) {
        // Return error if no block is free.
        if (b == ftl->num_blks)
            return (ui32)FsError2(FTL_NO_FREE_BLK, ENOSPC);

        // If block is free, return its block number.
        if (IS_FREE(ftl->bdata[b]))
            return b;
    }
}

// Global Function Definitions

// FtlnReport: Callback function used by upper file system layer to
//              notify FTL of events
//
//      Inputs: vol = FTL handle
//              msg = event
//              ... = additional arguments
//
//     Returns: 0 or 1 (unformat()) for success, -1 on failure
//
int FtlnReport(void* vol, ui32 msg, ...) {
    FTLN ftl = vol;
    va_list ap;

    // Set errno and return -1 if fatal I/O error occurred.
    if (ftl->flags & FTLN_FATAL_ERR)
        return FsError2(NDM_EIO, EIO);

    // Handle event passed down from file system layer.
    switch (msg) {
        case FS_UNFORMAT: {
            ui32 b;

            // Return error if volume is mounted.
            if (ftl->flags & FTLN_MOUNTED)
                return FsError2(FTL_MOUNTED, EEXIST);

            // Format volume. Return -1 if error.
            if (format_ftl(ftl))
                return -1;

            // Erase every unerased block. Return -1 if error.
            for (b = 0; b < ftl->num_blks; ++b)
                if ((ftl->bdata[b] & ERASED_BLK_FLAG) == FALSE)
                    if (FtlnEraseBlk(ftl, b))
                        return -1;

            // Delete volume (both FTL and FS). Free its memory. Volume is
            // unmounted, so nothing to flush. Return value can be ignored.
            FtlnDelVol(ftl);

            // Return '1' for success.
            return 1;
        }

        case FS_PAGE_SZ:
            return ftl->page_size;

        case FS_FORMAT:
        case FS_FORMAT_RESET_WC: {
            // Format volume. Return -1 if error.
            if (format_ftl(ftl))
                return -1;

            // Check if we're to equalize the wear counts (for benchmarking).
            if (msg == FS_FORMAT_RESET_WC) {
                ui32 b, avg_lag = 0;

                // Compute average wear count and assign to every block.
                for (b = 0; b < ftl->num_blks; ++b) avg_lag += ftl->blk_wc_lag[b];
                avg_lag /= ftl->num_blks;
                ftl->high_wc -= avg_lag;
                for (b = 0; b < ftl->num_blks; ++b) ftl->blk_wc_lag[b] = 0;
            }

            // Return success.
            return 0;
        }

        case FS_VCLEAN:
            return FtlnVclean(ftl);

        case FS_UNMOUNT:
            // Return error if not mounted.
            if ((ftl->flags & FTLN_MOUNTED) == FALSE)
                return FsError2(FTL_UNMOUNTED, ENOENT);

            // Clear the 'mounted' flag.
            ftl->flags &= ~FTLN_MOUNTED;
        // FALLTHROUGH

        case FS_SYNC: {
            // Prepare to write all dirty map cache pages. Return -1 if err.
            if (FtlnRecCheck(ftl, 0))
                return -1;

            // Save all dirty map pages to flash. Return -1 if error.
            if (ftlmcFlushMap(ftl->map_cache))
                return -1;
            PfAssert(ftl->num_free_blks >= FTLN_MIN_FREE_BLKS);

#if INC_FTL_NDM_MLC
            // For MLC devices, advance free_vpn pointer so next volume page
            // write can't corrupt previously written valid page.
            FtlnMlcSafeFreeVpn(ftl);
#endif

            // If request was for sync, return success now.
            if (msg == FS_SYNC)
                return 0;

#if INC_ELIST
            // Check if there is not a current erased-block list.
            if (ftl->elist_blk == (ui32)-1) {
                ui32 b, n;

                // Count the number of erased free blocks.
                for (n = b = 0; b < ftl->num_blks; ++b)
                    if (IS_ERASED(ftl->bdata[b]))
                        ++n;

                // Only write erased list if more than 1 block is erased.
                if (n > 1) {
                    ui32 wc, *lp, prior_free_mpn;
                    ui32* end = (ui32*)(ftl->main_buf + ftl->page_size);

                    // Save free map page number and force elist writes to begin
                    // on first page of a free map block.
                    prior_free_mpn = ftl->free_mpn;
                    ftl->free_mpn = (ui32)-1;

                    // Set pointer to write first entry on page.
                    lp = (ui32*)(ftl->main_buf + FTLN_META_DATA_BEG);

                    // Loop to find erased free blocks.
                    for (b = 0;;) {
                        if (IS_ERASED(ftl->bdata[b])) {
#if DEBUG_ELIST
                            // Verify that this block is unwritten.
                            FtlnCheckBlank(ftl, b);
#endif

                            // Write block number and wear count of erased block.
                            WR32_LE(b, lp);
                            ++lp;
                            wc = ftl->high_wc - ftl->blk_wc_lag[b];
                            WR32_LE(wc, lp);
                            ++lp;

                            // If all blocks recorded, fill rest of page with -1.
                            if (--n == 0)
                                while (lp != end) {
                                    WR32_LE(-1, lp);
                                    ++lp;
                                }

                            // Check if page is full.
                            if (lp == end) {
                                // Write page of erased list data.
                                if (FtlnMetaWr(ftl, ERASED_LIST))
                                    return -1;

                                // Break if all erased blocks have been recorded.
                                if (n == 0)
                                    break;

                                // Reset pointer to write next entry on new page.
                                lp = (ui32*)(ftl->main_buf + FTLN_META_DATA_BEG);

                                // Assert not at block end. That requires 16B pages.
                                PfAssert(ftl->free_mpn != (ui32)-1);
                            }
                        }

                        // Check if no blocks left to test.
                        if (++b == ftl->num_blks) {
                            // If unwritten data in last page, write it now.
                            if (lp != (ui32*)(ftl->main_buf + FTLN_META_DATA_BEG))
                                if (FtlnMetaWr(ftl, ERASED_LIST))
                                    return -1;

                            // List is finished, break.
                            break;
                        }
                    }

                    // Save elist block number and restore free map page number.
                    ftl->elist_blk = ftl->free_mpn / ftl->pgs_per_blk;
                    ftl->bdata[ftl->elist_blk] = FREE_BLK_FLAG;
                    ++ftl->num_free_blks;
                    ftl->free_mpn = prior_free_mpn;
                }
            }
#endif // INC_ELIST

#if FTLN_DEBUG > 1
            // Display FTL statistics.
            FtlnStats(ftl);
            FtlnBlkStats(ftl);
#endif

            // Return success.
            return 0;
        }

        case FS_FLUSH_SECT: {
            ui32 vsn, mpn;

            // Use the va_arg mechanism to get virtual sector to be flushed.
            va_start(ap, msg);
            vsn = va_arg(ap, ui32);
            va_end(ap);

            // Check argument for validity.
            PfAssert(vsn < ftl->num_vsects);

            // Figure out MPN this sector belongs to.
            mpn = (vsn / ftl->sects_per_page) / ftl->mappings_per_mpg;

            // Flush MPN from cache. Return -1 if error.
            if (ftlmcFlushPage(ftl->map_cache, mpn))
                return -1;

#if INC_FTL_NDM_MLC
            // For MLC devices, advance free_vpn pointer so next volume page
            // write can't corrupt previously written valid page.
            FtlnMlcSafeFreeVpn(ftl);
#endif

            // Return success.
            return 0;
        }

        case FS_MARK_UNUSED: {
            ui32 ppn, vsn, count, past_end, vpn;

            // Use va_arg mechanism to get the starting sector and number of
            // sectors to be invalidated.
            va_start(ap, msg);
            vsn = va_arg(ap, ui32);
            count = va_arg(ap, ui32);
            va_end(ap);

            // Check arguments for validity.
            if (vsn + count > ftl->num_vsects)
                return -1;

            // Compute first and one past last page that will be dirty.
            vpn = vsn / ftl->sects_per_page;
            past_end = vpn + count / ftl->sects_per_page;

            // Mark page(s) unused in FTL.
            for (; vpn < past_end; ++vpn) {
                // Prepare to potentially write 1 map page. Return -1 if error.
                if (FtlnRecCheck(ftl, -1))
                    return -1;

                // Retrieve physical page number for VPN. Return -1 if error.
                if (FtlnMapGetPpn(ftl, vpn, &ppn) < 0)
                    return -1;

                // If unmapped, skip page.
                if (ppn == (ui32)-1)
                    continue;

#if FS_ASSERT
                // Confirm no physical page number changes below.
                ftl->assert_no_recycle = TRUE;
#endif

                // Assign invalid value to VPN's physical page number and
                // decrement block's used page count.
                if (FtlnMapSetPpn(ftl, vpn, (ui32)-1))
                    return -1;
                PfAssert(ftl->num_free_blks >= FTLN_MIN_FREE_BLKS);
                FtlnDecUsed(ftl, ppn, vpn);

#if FS_ASSERT
                // End check for no physical page number changes.
                ftl->assert_no_recycle = FALSE;
#endif
            }

            // Return success.
            return 0;
        }

        case FS_VSTAT: {
            union vstat* buf;

            // Use the va_arg mechanism to get the vstat buffer.
            va_start(ap, msg);
            buf = (union vstat*)va_arg(ap, void*);
            va_end(ap);

            // Get the garbage level.
            buf->xfs.garbage_level = FtlnGarbLvl(ftl);

            // Get TargetFTL-NDM RAM usage.
            ftl->stats.ram_used = sizeof(struct ftln) + ftl->num_map_pgs * sizeof(ui32) +
                                  ftl->page_size + ftl->eb_size * ftl->pgs_per_blk +
                                  ftlmcRAM(ftl->map_cache) +
                                  ftl->num_blks * (sizeof(ui32) + sizeof(ui8));
#if FTLN_DEBUG > 1
            printf("TargetFTL-NDM RAM usage:\n");
            printf(" - sizeof(Ftln) : %u\n", (int)sizeof(FTLN));
            printf(" - tmp buffers  : %u\n", ftl->page_size + ftl->eb_size * ftl->pgs_per_blk);
            printf(" - map pages    : %u\n", ftl->num_map_pgs * 4);
            printf(" - map cache    : %u\n", ftlmcRAM(ftl->map_cache));
            printf(" - bdata[]      : %u\n", ftl->num_blks * (int)(sizeof(ui32) + sizeof(ui8)));
#endif

            // Record high wear count.
            ftl->stats.wear_count = ftl->high_wc;

            // Set TargetFTL-NDM driver call counts and reset internal ones.
            buf->xfs.drvr_stats.ftl.ndm = ftl->stats;
            buf->xfs.ftl_type = FTL_NDM;
            bzero(&ftl->stats, sizeof(ftl_ndm_stats));

            // Return success.
            return 0;
        }

        case FS_MOUNT:
            // Return error if already mounted. Else set mounted flag.
            if (ftl->flags & FTLN_MOUNTED)
                return FsError2(FTL_MOUNTED, EEXIST);
            ftl->flags |= FTLN_MOUNTED;

#if FTLN_DEBUG > 1
            // Display FTL statistics.
            FtlnStats(ftl);
            FtlnBlkStats(ftl);
#elif FTLN_DEBUG
            printf("FTL: total blocks: %u, free blocks: %u\n", ftl->num_blks, ftl->num_free_blks);
#endif

            // Return success.
            return 0;
    }

    // Return success.
    return 0;
}

#if INC_FTL_NDM_MLC
// FtlnMlcSafeFreeVpn: For MLC devices, ensure free_vpn pointer is
//              on a page whose pair is at a higher offset than the
//              last non-free page
//
//       Input: ftl = pointer to FTL control block
//
void FtlnMlcSafeFreeVpn(FTLN ftl) {
    // Only adjust MLC volumes for which volume free pointer is set.
    if (ftl->free_vpn != (ui32)-1) {
        ui32 pn = ndmPastPrevPair(ftl->ndm, ftl->free_vpn);

#if FTLN_DEBUG
        printf("FtlnMlcSafeFreeVpn: old free = %u, new free = %u\n", ftl->free_vpn, pn);
#endif
        ftl->free_vpn = pn;
    }
}
#endif // INC_FTL_NDM_MLC

// FtlnEraseBlk: Erase a block, increment its wear count, and mark it
//               free and erased
//
//      Inputs: ftl = pointer to FTL control block
//              b = block to erase
//
//     Returns: 0 on success, -1 on error
//
int FtlnEraseBlk(FTLN ftl, ui32 b) {
    ui32 b_wc;

#if INC_ELIST
    // Check if list of erased blocks/wear counts exists.
    if (ftl->elist_blk != (ui32)-1) {
        ui32 eb = ftl->elist_blk;

        // Forget erased list block number.
        ftl->elist_blk = (ui32)-1;

        // If not this block, erase it - because its info is out-of-date.
        if (eb != b)
            if (FtlnEraseBlk(ftl, eb))
                return -1;
    }
#endif

    // Call driver to erase block. Return -1 if error.
    ++ftl->stats.erase_block;
    if (ftl->erase_block(ftl->start_pn + b * ftl->pgs_per_blk, ftl->ndm))
        return FtlnFatErr(ftl);

    // Increment block wear count and possibly adjust highest.
    b_wc = ftl->high_wc - ftl->blk_wc_lag[b] + 1;
    if (ftl->high_wc < b_wc)
        set_high_wc(ftl, b, b_wc);
    else
        --ftl->blk_wc_lag[b];

    // If not free, increment free blocks count. Mark free and erased.
    if (IS_FREE(ftl->bdata[b]) == FALSE)
        ++ftl->num_free_blks;
    ftl->bdata[b] = FREE_BLK_FLAG | ERASED_BLK_FLAG;

    // Return success.
    return 0;
}

// FtlnLoWcFreeBlk: Find the free block with the lowest wear count
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: block number if successful, else (ui32)-1 if none free
//
ui32 FtlnLoWcFreeBlk(CFTLN ftl) {
    ui32 b, free_b;

    // Search for first free block. Return error if no block is free.
    free_b = first_free_blk(ftl);
    if (free_b == (ui32)-1)
        return free_b;

    // Continue search. Want free block with lowest wear count.
    for (b = free_b + 1; b < ftl->num_blks; ++b)
        if (IS_FREE(ftl->bdata[b]) && (ftl->blk_wc_lag[b] > ftl->blk_wc_lag[free_b]))
            free_b = b;

    // Return block number.
    return free_b;
}

// FtlnHiWcFreeBlk: Find the free block with the highest wear count
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: block number if successful, else (ui32)-1 if none free
//
ui32 FtlnHiWcFreeBlk(CFTLN ftl) {
    ui32 b, free_b;

    // Search for first free block. Return error if no block is free.
    free_b = first_free_blk(ftl);
    if (free_b == (ui32)-1)
        return free_b;

    // Continue search. Want free block with highest wear count.
    for (b = free_b + 1; b < ftl->num_blks; ++b)
        if (IS_FREE(ftl->bdata[b]) && (ftl->blk_wc_lag[b] < ftl->blk_wc_lag[free_b]))
            free_b = b;

    // Return block number.
    return free_b;
}

//  FtlnFormat: Erase all map blocks, mark all blocks free, and reset
//              the FTL (keeping wear offsets)
//
//      Inputs: ftl = pointer to FTL control block
//              meta_block = number of block holding the metapage
//
//     Returns: 0 on success, -1 on error
//
int FtlnFormat(FTLN ftl, ui32 meta_block) {
    ui32 b;

    PfAssert(meta_block < ftl->num_blks);
    // Erase all map blocks, except the one containing the metapage.
    for (b = 0; b < ftl->num_blks; ++b) {
        // Skip non-map blocks.
        if (!IS_MAP_BLK(ftl->bdata[b]))
            continue;

        // Skip block containing the metapage - this will be erased last.
        if (b == meta_block)
            continue;

        // Erase map block. Return -1 if error.
        if (FtlnEraseBlk(ftl, b))
            return -1;
    }

    // Erase the block holding the metapage: format finished!
    if (FtlnEraseBlk(ftl, meta_block))
        return -1;

    // Mark all non-erased blocks as free with zero read wear.
    for (b = 0; b < ftl->num_blks; ++b)
        if (!IS_FREE(ftl->bdata[b]))
            ftl->bdata[b] = FREE_BLK_FLAG;
    ftl->num_free_blks = ftl->num_blks;

    // Re-initialize volume state.
    FtlnStateRst(ftl);
    ftl->high_bc = 1;  // initial block count of unformatted volumes

#if FTLN_DEBUG
    // Display FTL statistics.
    FtlnBlkStats(ftl);
#endif

    // Return success.
    return 0;
}

// FtlnStateRst: Initialize volume state (except wear count offsets)
//
//       Input: ftl = pointer to FTL control block
//
void FtlnStateRst(FTLN ftl) {
    ui32 n;  // TIMER: was 'int'

    ftl->high_bc = 0;
    ftl->high_bc_mblk = ftl->resume_vblk = (ui32)-1;
    ftl->high_bc_mblk_po = 0;
    ftl->copy_end_found = FALSE;
    ftl->max_rc_blk = (ui32)-1;
    ftl->free_vpn = ftl->free_mpn = (ui32)-1;
#if INC_ELIST
    ftl->elist_blk = (ui32)-1;
#endif
    ftl->deferment = 0;
#if FTLN_DEBUG
    ftl->max_wc_lag = 0;
#endif
#if FS_ASSERT
    ftl->assert_no_recycle = FALSE;
#endif
    memset(ftl->spare_buf, 0xFF, ftl->pgs_per_blk * ftl->eb_size);
    for (n = 0; n < ftl->num_map_pgs; ++n) ftl->mpns[n] = (ui32)-1;
    ftlmcInit(ftl->map_cache);
}

// FtlnDecUsed: Decrement block used count for page no longer in-use
//
//      Inputs: ftl = pointer to FTL control block
//              pn = physical page number
//              vpn = virtual page number
//
void FtlnDecUsed(FTLN ftl, ui32 pn, ui32 vpn) {
    ui32 b = pn / ftl->pgs_per_blk;

    // Decrement block used count.
    PfAssert(NUM_USED(ftl->bdata[b]));
    PfAssert(!IS_FREE(ftl->bdata[b]));
    DEC_USED(ftl->bdata[b]);

#if FTLN_DEBUG
    // Read page spare area and assert VPNs match.
    ++ftl->stats.read_spare;
    PfAssert(ftl->read_spare(ftl->start_pn + pn, ftl->spare_buf, ftl->ndm) >= 0);
    PfAssert(GET_SA_VPN(ftl->spare_buf) == vpn);
#endif
} //lint !e818

//  FtlnFatErr: Process FTL-NDM fatal error
//
//       Input: ftl = pointer to FTL control block
//
//     Returns: -1
//
int FtlnFatErr(FTLN ftl) {
    ftl->flags |= FTLN_FATAL_ERR;
    return FsError2(NDM_EIO, EIO);
}

#if FTLN_DEBUG
// flush_bstat: Flush buffered statistics counts
//
//      Inputs: ftl = pointer to FTL control block
//              b = block number of current block
//              type = "FREE", "MAP", or "VOLUME"
//  In/Outputs: *blk0 = first consecutive block number or -1
//              *blke = end consecutive block number
//
static void flush_bstat(CFTLN ftl, int* blk0, int* blke, int b, const char* type) {
    if (*blk0 == -1)
        *blk0 = *blke = b;
    else if (*blke + 1 == b)
        *blke = b;
    else {
        printf("B = %4u", *blk0);
        if (*blk0 == *blke) {
            printf(" - used = %2u, wc lag = %3d, rc = %8u", NUM_USED(ftl->bdata[*blk0]),
                   ftl->blk_wc_lag[*blk0], GET_RC(ftl->bdata[*blk0]));
            printf(" - %s BLOCK\n", type);
        } else {
            printf("-%-4u", *blke);
            printf("%*s", 37, " ");
            printf("- %s BLOCKS\n", type);
        }
        *blk0 = *blke = b;
    }
}

// FtlnBlkStats: Debug function to display blocks statistics
//
//       Input: ftl = pointer to FTL control block
//
void FtlnBlkStats(CFTLN ftl) {
    int free0 = -1, freee, vol0 = -1, vole;
    ui32 b;

    printf(
        "\nBLOCK STATS: %u blocks, %u pages per block, curr free "
        "blocks = %u\n",
        ftl->num_blks, ftl->pgs_per_blk, ftl->num_free_blks);

    // Loop over FTL blocks.
    for (b = 0; b < ftl->num_blks; ++b) {
        // Check if block is free.
        if (IS_FREE(ftl->bdata[b])) {
            flush_bstat(ftl, &vol0, &vole, -1, "VOLUME");
            flush_bstat(ftl, &free0, &freee, b, "FREE");
        }

        // Else check if map block.
        else if (IS_MAP_BLK(ftl->bdata[b])) {
            flush_bstat(ftl, &free0, &freee, -1, "FREE");
            flush_bstat(ftl, &vol0, &vole, -1, "VOLUME");
            printf("B = %4u - used = %2u, wc lag = %3d, rc = %8u - ", b, NUM_USED(ftl->bdata[b]),
                   ftl->blk_wc_lag[b], GET_RC(ftl->bdata[b]));
            printf("MAP BLOCK\n");
        }

        // Else is volume block.
        else {
            flush_bstat(ftl, &free0, &freee, -1, "FREE");
#if FTLN_DEBUG <= 1
            flush_bstat(ftl, &vol0, &vole, b, "VOLUME");
#else
            printf("B = %4u - used = %2u, wc lag = %3d, rc = %8u - ", b, NUM_USED(ftl->bdata[b]),
                   ftl->blk_wc_lag[b], GET_RC(ftl->bdata[b]));
            printf("VOLUME BLOCK\n");
#endif
        }
    }
    flush_bstat(ftl, &free0, &freee, -1, "FREE");
    flush_bstat(ftl, &vol0, &vole, -1, "VOLUME");
}
#endif // FTLN_DEBUG

#if FTLN_DEBUG > 1
//   FtlnStats: Display FTL statistics
//
//       Input: ftl = pointer to FTL control block
//
void FtlnStats(FTLN ftl) {
    ui32 b, n;

    printf("\nFTL STATS:\n");
    printf("  - # vol sects    = %d\n", ftl->num_vsects);
    printf("  - # vol pages    = %d\n", ftl->num_vpages);
    printf("  - # map pages    = %d\n", ftl->num_map_pgs);
    printf("  - # free blocks  = %d\n", ftl->num_free_blks);
    for (n = b = 0; b < ftl->num_blks; ++b)
        if (IS_ERASED(ftl->bdata[b]))
            ++n;
    printf("  - # erased blks  = %d\n", n);
    printf("  - flags =");
    if (ftl->flags & FTLN_FATAL_ERR)
        printf(" FTLN_FATAL_ERR");
    if (ftl->flags & FTLN_MOUNTED)
        printf(" FTLN_MOUNTED");
    putchar('\n');
}
#endif // FTLN_DEBUG

#if DEBUG_ELIST
// FtlnCheckBlank: Ensure the specified block is blank
//
//      Inputs: ftl = pointer to FTL control block
//              b = block number of block to check
//
void FtlnCheckBlank(FTLN ftl, ui32 b) {
    ui32 pn = b * ftl->pgs_per_blk;
    ui32 end = pn + ftl->pgs_per_blk;
    int rc;

    do {
        rc = ftl->page_check(pn, ftl->main_buf, ftl->spare_buf, ftl->ndm);
        PfAssert(rc == NDM_PAGE_ERASED);
    } while (++pn < end);
}
#endif // DEBUG_ELIST

