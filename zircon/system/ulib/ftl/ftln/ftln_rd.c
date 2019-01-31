// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftlnp.h"

#if INC_FTL_NDM
// Type Definitions
typedef struct {
    ui32 first_ppn;  // first physical page number or -1
    ui32 run_cnt;    // number of staged page reads
    ui8* buf;        // pointer to output buffer
} StagedRd;

// Local Function Definitions

#if INC_SECT_FTL
// partial_page_read: Read some sectors from a virtual page
//
//      Inputs: ftl = pointer to FTL control block
//              vpn = virtual page number
//              byte_offset = first sector's offset into page
//              byte_count = number of bytes to read from page
//              buf = pointer to where data is copied to
//
//     Returns: 0 on success, -1 on failure
//
static int partial_page_read(FTLN ftl, ui32 vpn, uint byte_offset, uint byte_count, ui8* buf) {
    ui32 pn;

#if INC_FTL_PAGE_CACHE
    // Check if there is a volume page cache.
    if (ftl->vol_cache) {
        FcEntry* cache_ent;

        // Attempt to retrieve page via cache. If error, return.
        cache_ent = ftlvcGetRdPage(ftl->vol_cache, vpn);
        if (cache_ent == (void*)-1)
            return -1;

        // If page cached, retrieve its contents and return success.
        if (cache_ent) {
            memcpy(buf, &cache_ent->data[byte_offset], byte_count);
            FcFreeEntry(ftl->vol_cache, &cache_ent);
            return 0;
        }
    }
#endif

    // Prepare to potentially write one map page. Return -1 if error.
    if (FtlnRecCheck(ftl, -1))
        return -1;

    // Convert the virtual page number to its physical page number.
    if (FtlnMapGetPpn(ftl, vpn, &pn) < 0)
        return -1;

    // If unmapped, output data values as for unwritten data.
    if (pn == (ui32)-1)
        memset(buf, 0xFF, byte_count);

    // Else read page from flash and copy relevant sectors from page.
    else {
        if (FtlnRdPage(ftl, pn, ftl->swap_page))
            return -1;
        memcpy(buf, &ftl->swap_page[byte_offset], byte_count);
    }

    // Return success.
    return 0;
}
#endif // INC_SECT_FTL

// flush_pending_reads: Read all pages that are pending
//
//      Inputs: ftl = pointer to FTL control block
//              staged = pointer to structure holding 1st page number,
//                       page count, and output buffer pointer
//
//     Returns: 0 on success, -1 on error
//
static int flush_pending_reads(FTLN ftl, StagedRd* staged) {
    int status;
    ui32* b_ptr;

    // Issue pending reads.
    ftl->stats.read_page += staged->run_cnt;
    status = ftl->read_pages(ftl->start_pn + staged->first_ppn, staged->run_cnt, staged->buf,
                             ftl->spare_buf, ftl->ndm);

    // Adjust data buffer pointer.
    staged->buf += staged->run_cnt * ftl->page_size;

    // Get handle on blocks[] entry and increment block wear count.
    b_ptr = &ftl->bdata[staged->first_ppn / ftl->pgs_per_blk];
    INC_RC(ftl, b_ptr, staged->run_cnt);

    // Check if error was reported.
    if (status) {
        // If block needs to be recycled, set block read count to its max.
        if (status == 1) {
            SET_MAX_RC(ftl, b_ptr);
            status = 0;
        }

        // Else if fatal error, set errno and fatal I/O flag, return -1.
        else if (status == -2)
            return FtlnFatErr(ftl);
    }

    // Reset pending sequence and return status.
    staged->first_ppn = (ui32)-1;
    staged->run_cnt = 0;
    return status;
}

// read_sectors: Read as many virtual sectors as possible/needed from
//              an FTL page
//
//      Inputs: ftl = pointer to FTL control block
//              vsn = first virtual sector to read
//              count = number of consecutive sectors to read
//              data = pointer to where data is copied to
//
//     Returns: 0 on success, -1 on error
//
static int read_sectors(FTLN ftl, ui32 vsn, ui32 count, ui8* data) {
#if INC_SECT_FTL
    ui32 sect_cnt, sect_offset;
#endif
    ui32 vpn;
    StagedRd staged;

    // Initialize structure for staging deferred consecutive page reads.
    staged.buf = data;
    staged.run_cnt = 0;
    staged.first_ppn = (ui32)-1;

    // Set errno and return -1 if fatal I/O error occurred.
    if (ftl->flags & FTLN_FATAL_ERR)
        return FsError(EIO);

    // Get offset in page to first sector and its virtual page number.
    vpn = vsn / ftl->sects_per_page;
#if INC_SECT_FTL
    sect_offset = vsn % ftl->sects_per_page;

    // Determine the number of sectors to read from the first page.
    sect_cnt = ftl->sects_per_page - sect_offset;
    if (sect_cnt > count)
        sect_cnt = count;

    // Check if skipping some sectors on first page.
    if (sect_cnt < ftl->sects_per_page) {
        uint byte_offset = sect_offset * ftl->sect_size;
        uint byte_count = sect_cnt * ftl->sect_size;

        // Perform partial read. If error return -1.
        if (partial_page_read(ftl, vpn, byte_offset, byte_count, staged.buf))
            return -1;

        // Adjust buffer pointer and counters for successful partial read.
        staged.buf += byte_count;
        ++vpn;
        count -= sect_cnt;
    }

    // Check if any whole page reads are left.
    if (count >= ftl->sects_per_page)
#endif
    {
        ui32 pn;

        // Loop to read whole pages.
        do {
            // Check if reads are staged and PPN lookup could cause recycle.
            if (staged.run_cnt) {
                // If next PPN lookup could cause recycle, flush saved PPNs.
                if (FtlnRecNeeded(ftl, -1)) {
                    if (flush_pending_reads(ftl, &staged))
                        return -1;
                }

#if FS_ASSERT
                // Else confirm no physical page number changes due to recycle.
                else
                    ftl->assert_no_recycle = TRUE;
#endif
            }

            // Prepare to potentially write one map page. Return -1 if error.
            if (FtlnRecCheck(ftl, -1))
                return -1;

            // Convert the virtual page number to its physical page number.
            if (FtlnMapGetPpn(ftl, vpn, &pn) < 0)
                return -1;

#if FS_ASSERT
            // End check for no physical page number changes.
            ftl->assert_no_recycle = FALSE;
#endif

            // Check if page is unmapped.
            if (pn == (ui32)-1) {
                // Flush pending reads if any.
                if (staged.first_ppn != (ui32)-1)
                    if (flush_pending_reads(ftl, &staged))
                        return -1;

                // Fill page's sectors with the value for unwritten data and
                // advance buffer pointer.
                memset(staged.buf, 0xFF, ftl->page_size);
                staged.buf += ftl->page_size;
            }

            // Else have valid mapped page number.
            else {
#if INC_FTL_PAGE_CACHE
                FcEntry* cache_ent = NULL;

                // If there is a volume page cache, check if page is cached.
                if (ftl->vol_cache) {
                    // If page in cache, use cached version.
                    cache_ent = FcInCache(ftl->vol_cache, vpn);
                    if (cache_ent) {
                        // Flush pending reads, if any. Return -1 if error.
                        if (staged.first_ppn != (ui32)-1)
                            if (flush_pending_reads(ftl, &staged))
                                return -1;

                        // Copy page contents and advance data buffer.
                        memcpy(staged.buf, cache_ent->data, ftl->page_size);
                        staged.buf += ftl->page_size;
                    }
                }

                // Check if page is uncached.
                if (cache_ent == NULL)
#endif
                {
                    // If next in sequence and in same block, add page to list.
                    if ((staged.first_ppn + staged.run_cnt == pn) &&
                        (staged.first_ppn / ftl->pgs_per_blk == pn / ftl->pgs_per_blk))
                        ++staged.run_cnt;

                    // Else flush pending reads, if any, and start new list.
                    else {
                        if (staged.first_ppn != (ui32)-1)
                            if (flush_pending_reads(ftl, &staged))
                                return -1;
                        staged.first_ppn = pn;
                        staged.run_cnt = 1;
                    }
                }
            }

            // Adjust virtual page number and sector count.
            ++vpn;
            count -= ftl->sects_per_page;
        } while (count >= ftl->sects_per_page);

        // Flush pending reads if any.
        if (staged.first_ppn != (ui32)-1)
            if (flush_pending_reads(ftl, &staged))
                return -1;
    }

#if INC_SECT_FTL
    // Check if partial read is left.
    if (count)
        if (partial_page_read(ftl, vpn, 0, count * ftl->sect_size, staged.buf))
            return -1;
#endif

    // Return success.
    return 0;
}

// Global Function Definitions

#if INC_FTL_PAGE_CACHE
//   FtlnVpnRd: Cache function to read a volume page
//
//      Inputs: buffer = place to store read data
//              vpn = volume page number
//              vol_ptr = FTL handle
//
//     Returns: 0 on success, -1 on failure
//
int FtlnVpnRd(void* buffer, ui32 vpn, void* vol_ptr) {
    FTLN ftl = vol_ptr;

    return read_sectors(ftl, vpn * ftl->sects_per_page, ftl->sects_per_page, buffer);
}
#endif

// FtlnRdSects: Read count worth of virtual sectors from FTL
//
//      Inputs: buffer = place to store data bytes from read sectors
//              sect = first sector to read from
//              count = number of consecutive sectors to read
//              vol = FTL handle
//
//     Returns: 0 on success, -1 on failure
//
int FtlnRdSects(void* buffer, ui32 sect, int count, void* vol) {
    FTLN ftl = vol;

    // Ensure request is within volume's range of provided sectors.
    if (sect + count > ftl->num_vsects)
        return FsError(ENOSPC);

    // If no sectors to read, return success.
    if (count == 0)
        return 0;

    // If there's at least a block with a maximum read count, recycle.
    if (ftl->max_rc_blk != (ui32)-1)
        if (FtlnRecCheck(ftl, 0))
            return -1;

#if INC_FAT_MBR
    // Ensure all cluster requests are page aligned.
    if (sect >= ftl->frst_clust_sect)
        sect += ftl->clust_off;
#endif

    // Read sectors and return status.
    return read_sectors(ftl, sect, count, buffer);
}

//  FtlnMapRd: Read an MPN from flash - used by MPN cache
//
//      Inputs: vol = FTL handle
//              mpn = map page to read
//              buf = buffer to hold contents of map page
//      Output: *unmapped = TRUE iff page is unmapped
//
//     Returns: 0 on success, -1 on error
//
int FtlnMapRd(void* vol, ui32 mpn, void* buf, int* unmapped) {
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

//  FtlnRdPage: Read one page from flash and check return status
//
//      Inputs: ftl = pointer to FTL control block
//              ppn = physical page number of page to read from flash
//              rd_buf = buffer to hold read contents
//
//     Returns: 0 on success, -1 on error
//
int FtlnRdPage(FTLN ftl, ui32 ppn, void* rd_buf) {
    int status;
    ui32* b_ptr;

    // Set errno and return -1 if fatal I/O error occurred.
    if (ftl->flags & FTLN_FATAL_ERR)
        return FsError(EIO);

    // Read page from flash. If error, set errno/fatal flag/return -1.
    ++ftl->stats.read_page;
    status = ftl->read_pages(ftl->start_pn + ppn, 1, rd_buf, ftl->spare_buf, ftl->ndm);
    if (status < 0)
        return FtlnFatErr(ftl);

    // Get handle on block entry in blocks[].
    b_ptr = &ftl->bdata[ppn / ftl->pgs_per_blk];

    // If recycle requested, set read count to max. Else increment it.
    if (status)
        SET_MAX_RC(ftl, b_ptr);
    else
        INC_RC(ftl, b_ptr, 1);

    // Return success.
    return 0;
}
#endif // INC_FTL_NDM
