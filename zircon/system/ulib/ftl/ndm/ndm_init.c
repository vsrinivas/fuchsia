// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl.h"
#include "ndmp.h"

// Symbol Definitions
#define NDM_META_BLKS 2  // blocks reserved for internal use

// Count number of bits set to 1 in a byte/32 bit value
#define ONES_UI8(b) (NumberOnes[(b) >> 4] + NumberOnes[(b)&0xF])
#define ONES_UI32(w)                                                             \
  (ONES_UI8(((ui8*)&w)[0]) + ONES_UI8(((ui8*)&w)[1]) + ONES_UI8(((ui8*)&w)[2]) + \
   ONES_UI8(((ui8*)&w)[3]))

// Global Variable Declarations
CircLink NdmDevs;
SEM NdmSem;
static int NdmSemCount;
#if NV_NDM_CTRL_STORE
static ui8 NdmDevCnt;
#endif
// Lookup for number of bits in half byte.
static const ui8 NumberOnes[] = {
    // 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
};

// Local Function Definitions

// get_page_status: Examine page to see if it is an NDM control page
//
//      Inputs: ndm = pointer to NDM control block
//              pn = page to examine
//
//     Returns: -1 if I/O error, NDM_CTRL_BLOCK (2) if control page,
//              or NDM_REG_BLOCK (3) if ECC, CRC, sig, or flag fails
//
static int get_page_status(CNDM ndm, ui32 pn) {
  int i, j, status;
  ui32 crc;

  // Read spare area to check page type. Use read_decode_spare() if
  // decode is 'free', to avoid second read. Return -1 if I/O error,
  // regular block if ECC error.
  if (FLAG_IS_CLR(ndm->flags, FSF_FREE_SPARE_ECC)) {
    status = ndm->read_spare(pn, ndm->spare_buf, ndm->dev);
  } else {
    status = ndm->read_decode_spare(pn, ndm->spare_buf, ndm->dev);
    if (status == -1)
      return NDM_REG_BLOCK;
  }
  if (status < 0)
    return FsError2(NDM_EIO, EIO);

  // Block is regular block if regular page mark is not cleared.
  if (ONES_UI8(ndm->spare_buf[EB_REG_MARK]) >= 7)
    return NDM_REG_BLOCK;

  // If not done already, read decode spare area. Return -1 if fatal
  // error, regular block if ECC error.
  if (FLAG_IS_CLR(ndm->flags, FSF_FREE_SPARE_ECC)) {
    status = ndm->read_decode_spare(pn, ndm->spare_buf, ndm->dev);
    if (status == -2)
      return FsError2(NDM_EIO, EIO);
    else if (status == -1)
      return NDM_REG_BLOCK;
  }

  // Check signature in spare area to ensure this is a control page.
  for (i = j = 0; i < CTRL_SIG_SZ; ++i) {
    // Skip the bad block mark in extra bytes.
    if (i == EB_BBLOCK_MARK)
      ++j;

    // Block is regular block if signature is invalid.
    if (ndm->spare_buf[i + j] != CTRL_SIG[i])
      return NDM_REG_BLOCK;
  }

  // Read main data. Return -1 if fatal err, regular block if ECC err.
  status = ndm->read_page(pn, ndm->main_buf, ndm->spare_buf, ndm->dev);
  if (status == -2)
    return FsError2(NDM_EIO, EIO);
  else if (status == -1)
    return NDM_REG_BLOCK;

  crc = ndmReadControlCrc(ndm);

  // If the CRC does not match, page is not control page.
  if (crc != CRC32_FINAL)
    return NDM_REG_BLOCK;

  // Valid signature found. This is a control block.
  return NDM_CTRL_BLOCK;
}

// format_status: Check if NDM device is formatted
//
//      Inputs: ndm = pointer to NDM control block
//
//     Returns: 0 if formatted, else -1 with error code in FsErrCode
//
//        Note: If found, metadata block # is saved in ctrl_blk0
//
static int format_status(NDM ndm) {
  ui32 b;
  int status;

  // Search reserved good blocks for control info, starting from end.
  for (b = ndm->num_dev_blks - 1;; --b) {
    ui32 pn = b * ndm->pgs_per_blk;

    // Get block's initial good/bad status. Return -1 if error.
    status = ndm->is_block_bad(pn, ndm->dev);
    if (status < 0)
      return FsError2(NDM_EIO, EIO);

    // If good, check block's first page for control information.
    if (status == FALSE) {
      // Get page status. Return -1 if error.
      status = get_page_status(ndm, pn);
      if (status == -1)
        return -1;

      // If control info found, device is formatted. Return TRUE.
      if (status == NDM_CTRL_BLOCK) {
#if NDM_DEBUG
        ndm->logger.debug("NDM formatted - block #%u has control info!", b);
#endif
        PfAssert(ndm->ctrl_blk0 == (ui32)-1);
        ndm->ctrl_blk0 = b;
        return 0;
      }
    }

    // Return FALSE if no metadata in range used for control blocks.
    if (b == ndm->num_dev_blks - NDM_META_BLKS - ndm->max_bad_blks)
      return FsError2(NDM_NO_META_BLK, ENXIO);
  }
}

// get_free_ctrl_blk: Get next free block reserved for replacing bad
//              control blocks (starts at highest and goes down)
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: Block number of free block or (ui32)-1 if none left
//
static ui32 get_free_ctrl_blk(NDM ndm) {
  ui32 free_b = ndm->free_ctrl_blk;

  // Move free control block pointer one down, if any free blocks left.
  if (free_b != (ui32)-1) {
    ui32 b = free_b - 1;

    // Skip past initial bad blocks.
    while (b >= ndm->free_virt_blk && ndmInitBadBlock(ndm, b))
      --b;

    // If above the blocks reserved for swapping bad virtual blocks,
    // update the free control block pointer. Else fail.
    if (b >= ndm->free_virt_blk)
      ndm->free_ctrl_blk = b;
    else
      ndm->free_virt_blk = ndm->free_ctrl_blk = (ui32)-1;
  }

  // Return free control block number or -1.
  return free_b;
}

// set_frst_ndm_block: Compute the cutoff point between virtual area
//              and the NDM reserved area
//
//       Input: ndm = pointer to NDM control block
//
static void set_frst_ndm_block(NDM ndm) {
  ui32 i;

  // There must be enough good (non-initial bad) blocks before the cut
  // off point to hold all the virtual blocks. Find the lowest offset
  // past the virtual block count that satisfies this.
  for (i = 0;; ++i) {
    // If the offset reaches the number of initially bad blocks, then
    // there are definitely num_vblks good blocks below this cutoff.
    if (i == ndm->num_bad_blks)
      break;

    // 'i' is the number of initial bad blocks preceding the indexed
    // initial bad block. Break when the number of volume blocks and
    // skipped bad blocks is less than the indexed initial bad block.
    if (ndm->num_vblks + i < ndm->init_bad_blk[i])
      break;
  }

  // The cutoff point is num_vblks plus the determined offset.
  ndm->frst_reserved = ndm->num_vblks + i;
}

// init_ibad_list: Initialize list of initially bad blocks
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on error
//
static int init_ibad_list(NDM ndm) {
  int status;
  ui32 b;

  // Build the initial bad block map by scanning all blocks in order
  // from lowest to highest. Supports "skip bad block" programming.
  ndm->num_bad_blks = 0;
  for (b = 0; b < ndm->num_dev_blks; ++b) {
    // Get block's initial good/bad status. Return -1 if error.
    status = ndm->is_block_bad(b * ndm->pgs_per_blk, ndm->dev);
    if (status < 0)
      return FsError2(NDM_EIO, EIO);

    // Check if block is an initial bad block.
    if (status == TRUE) {
      // If too many bad blocks encountered, error. Return -1.
      if (ndm->num_bad_blks >= ndm->max_bad_blks)
        return FsError2(NDM_TOO_MANY_IBAD, EINVAL);

      // Add block to initial bad blocks array and increment bad count.
      ndm->init_bad_blk[ndm->num_bad_blks] = b;
#if NDM_DEBUG
      ndm->logger.debug("init_ibad_lis: adding block #%u to init_bad_blk[%u]", b,
                        ndm->num_bad_blks);
#endif
      ++ndm->num_bad_blks;
    }
  }

  // Set the last initial bad block entry to the device block count.
  ndm->init_bad_blk[ndm->num_bad_blks] = ndm->num_dev_blks;
#if NDM_DEBUG
  ndm->logger.debug("init_ibad_lis: LAST init_bad_blk[%u] = %u", ndm->num_bad_blks,
                    ndm->num_dev_blks);
#endif

  // Return success.
  return 0;
}

//  ndm_format: Format previously unformatted device
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on error
//
static int ndm_format(NDM ndm) {
  ui32 b;

#if NV_NDM_CTRL_STORE
  // Invalidate the saved first page of NDM control information.
  NvNdmCtrlPgWr(0);
#endif

  // Build the initial bad block map by scanning all blocks in order.
  if (init_ibad_list(ndm))
    return -1;

  // Compute the cutoff between virtual blocks and reserved blocks.
  set_frst_ndm_block(ndm);

  // Set the free control block (last good block) and free volume
  // block (first good block after cutoff) pointers.
  for (b = ndm->frst_reserved; b < ndm->num_dev_blks; ++b) {
    // If initial bad block, skip it.
    if (ndmInitBadBlock(ndm, b))
      continue;

    // If first free block pointer is not set, set it.
    if (ndm->free_virt_blk == (ui32)-1)
      ndm->free_virt_blk = b;

    // Set the last good block as the start of the metadata blocks.
    ndm->free_ctrl_blk = b;
  }

  // The last two good free blocks are used for control information.
  ndm->ctrl_blk0 = get_free_ctrl_blk(ndm);
  ndm->ctrl_blk1 = get_free_ctrl_blk(ndm);
  if (ndm->ctrl_blk1 == (ui32)-1)
    return FsError2(NDM_NO_FREE_BLK, ENOSPC);
#if NDM_DEBUG
  ndm->logger.debug("NDM ctrl_blk0=%u, ctrl_blk1=%u", ndm->ctrl_blk0, ndm->ctrl_blk1);
#endif

  // Begin the first control write on ctrl_blk0.
  ndm->next_ctrl_start = ndm->ctrl_blk0 * ndm->pgs_per_blk;

  // Write initial control information and return status.
  ndm->xfr_tblk = (ui32)-1;
  ndm->version_2 = ndm->format_with_v2;
  return ndmWrCtrl(ndm);
}

// read_header_values: Reads info from a control block header.
//
//       Input: ndm = pointer to NDM control block
//
static void read_header_values(CNDM ndm, ui16* current_page, ui16* last_page, ui32* sequence) {
  ui32 current_location = HDR_CURR_LOC;
  ui32 last_location = HDR_LAST_LOC;
  ui32 sequence_location = HDR_SEQ_LOC;

  // Shift header data for version 2.
  if (RD16_LE(&ndm->main_buf[0]) != 1) {
    current_location += HDR_V2_SHIFT;
    last_location += HDR_V2_SHIFT;
    sequence_location += HDR_V2_SHIFT;
  }

  // If matching first control page found, return success.
  *current_page = RD16_LE(&ndm->main_buf[current_location]);
  *last_page = RD16_LE(&ndm->main_buf[last_location]);
  *sequence = RD32_LE(&ndm->main_buf[sequence_location]);
}

// find_last_ctrl_info: Find last valid control information
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 if found, -1 otherwise
//
static int find_last_ctrl_info(NDM ndm) {
  ui32 b, p, high_seq = (ui32)-1, curr_seq;
  ui32 p_beg, p_end, last_ctrl_p = 0, ctrl_pages = 0;
  ui16 curr_p, last_p;
  int page_status;

#if NV_NDM_CTRL_STORE
  // Check if number of first control information page has been saved.
  p = NvNdmCtrlPgRd();
  if (p) {
    // Get the page status. If I/O error, return -1.
    page_status = get_page_status(ndm, p);
    if (page_status == -1)
      return -1;

    // Check if it is a control page.
    if (page_status == NDM_CTRL_BLOCK) {
      read_header_values(ndm, &curr_p, &ctrl_pages, &high_seq);

      // Check if it is the last page of a control information write.
      if (curr_p == ctrl_pages) {
        // Read its (the highest) sequence number and use this page.
        last_ctrl_p = p;
      }
    }
  }

  // If last control page not known from NVRAM, search all reserved
  // blocks for it: from ctrl_blk0 (highest block w/control info) to
  // num_vblks.
  if (last_ctrl_p == 0)
#endif
  {
    for (b = ndm->ctrl_blk0; b >= ndm->num_vblks; --b) {
      // Get first and last page numbers to search.
      p_beg = b * ndm->pgs_per_blk;
      p_end = p_beg + ndm->pgs_per_blk - 1;

      // Check if not block that format_status() found metadata on.
      if (b != ndm->ctrl_blk0) {
        // Get status of block's first page. If error, return -1.
        page_status = get_page_status(ndm, p_beg);
        if (page_status == -1)
          return -1;

        // Skip block if its first page is not a control page.
        if (page_status != NDM_CTRL_BLOCK)
          continue;
      }

      // Search block from end to beginning for last control page.
      for (p = p_end; p >= p_beg; --p) {
        // Get the page status. If error, return -1.
        page_status = get_page_status(ndm, p);
        if (page_status == -1)
          return -1;

        // If page is not control page, skip it.
        if (page_status != NDM_CTRL_BLOCK)
          continue;

        read_header_values(ndm, &curr_p, &last_p, &curr_seq);

        // If this is not the last page of a control info, skip it.
        if (curr_p != last_p)
          continue;

        // If first 'last page' found or most recent, remember it.
        if (high_seq == (ui32)-1 || curr_seq > high_seq) {
          // Remember its sequence number and first/last page numbers.
          high_seq = curr_seq;
          last_ctrl_p = p;
          ctrl_pages = last_p;
#if NDM_DEBUG
          ndm->logger.debug(
              "find_ctrl: seq #%u, last = %u (block = %u), "
              "# pages = %u",
              high_seq, last_ctrl_p, last_ctrl_p / ndm->pgs_per_blk, ctrl_pages);
#endif
        }

        // Break to search next block.
        break;
      }
    }
  }

  // If no last control page found, no control information on device.
  if (high_seq == (ui32)-1)
    return FsError2(NDM_NO_META_DATA, ENXIO);

  // Save information found so far.
  ndm->last_ctrl_page = last_ctrl_p;
  ndm->ctrl_pages = ctrl_pages;
  ndm->ctrl_seq = high_seq;

  // If control information is only one page, finish (success!) here.
  if (ctrl_pages == 1) {
    ndm->frst_ctrl_page = last_ctrl_p;
    return 0;
  }

  // Search for first page of latest control info in block with last.
  p_end = (last_ctrl_p / ndm->pgs_per_blk) * ndm->pgs_per_blk;
  for (p = last_ctrl_p - 1; p >= p_end; --p) {
    // Get the page status. If error, return -1.
    page_status = get_page_status(ndm, p);
    if (page_status == -1)
      return -1;

    // If page is not control page, skip it.
    if (page_status != NDM_CTRL_BLOCK)
      continue;

    read_header_values(ndm, &curr_p, &last_p, &curr_seq);

    if (curr_p == 1 && curr_seq == high_seq && last_p == ctrl_pages) {
#if NDM_DEBUG
      ndm->logger.debug("find_ctrl: first = %u (block = %u)", p, p / ndm->pgs_per_blk);
#endif
      ndm->frst_ctrl_page = p;
      return 0;
    }
  }

  // First page wasn't found, scan all other NDM reserved blocks.
  for (b = ndm->ctrl_blk0; b >= ndm->num_vblks; --b) {
    // Skip the scanned block.
    if (b == last_ctrl_p / ndm->pgs_per_blk)
      continue;

    // Get first and last page numbers to search.
    p_beg = b * ndm->pgs_per_blk;
    p_end = p_beg + ndm->pgs_per_blk - 1;

    // Start scanning the pages in the block.
    for (p = p_beg; p <= p_end; ++p) {
      // Get the page status. If error, return -1.
      page_status = get_page_status(ndm, p);
      if (page_status == -1)
        return -1;

      // If page is not control page, skip it.
      if (page_status != NDM_CTRL_BLOCK)
        continue;

      read_header_values(ndm, &curr_p, &last_p, &curr_seq);

      if (curr_p == 1 && curr_seq == high_seq && last_p == ctrl_pages) {
#if NDM_DEBUG
        ndm->logger.debug("find_ctrl: first = %u (block = %u)", p, p / ndm->pgs_per_blk);
#endif
        ndm->frst_ctrl_page = p;
        return 0;
      }
    }
  }

  // First control page not found, return -1.
  return FsError2(NDM_NO_META_DATA, ENXIO);
}

// is_next_ctrl_page: Determines if page is next in control sequence.
//
//      Inputs: ndm = pointer to NDM control block.
//              pn = page to analyze,
//              curr_num = current number in control sequence.
//
//     Returns: NDM_CTRL_BLOCK iff page next one, NDM_REG_BLOCK if not,
//             -1 on error.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static int is_next_ctrl_page(CNDM ndm, ui32 pn, ui16 curr_num) {
  int page_status;

  const ui32 current_location = ndmGetHeaderCurrentLocation(ndm);
  const ui32 last_location = ndmGetHeaderLastLocation(ndm);
  const ui32 sequence_location = ndmGetHeaderSequenceLocation(ndm);

  // Get the page status.
  page_status = get_page_status(ndm, pn);
  if (page_status != NDM_CTRL_BLOCK)
    return page_status;

  // Read page in. Return -1 if error (ECC or fatal).
  if (ndm->read_page(pn, ndm->main_buf, ndm->spare_buf, ndm->dev) < 0)
    return FsError2(NDM_EIO, EIO);

  // Determine if this is the next control page in sequence.
  if (RD16_LE(&ndm->main_buf[current_location]) == curr_num + 1 &&
      RD16_LE(&ndm->main_buf[last_location]) == ndm->ctrl_pages &&
      RD32_LE(&ndm->main_buf[sequence_location]) == ndm->ctrl_seq) {
    return NDM_CTRL_BLOCK;
  }

  // Else this is a regular block.
  return NDM_REG_BLOCK;
}

// get_next_ctrl_page: Retrieves next page in control info.
//
//      Inputs: ndm = pointer to NDM control block.
//              curr_p = current control page.
//
//     Returns: Next control page, -1 on error.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static ui32 get_next_ctrl_page(CNDM ndm, ui32 curr_p) {
  ui16 curr_num;
  ui32 p, p_end;
  int page_status;

  const ui32 current_location = ndmGetHeaderCurrentLocation(ndm);

  // Retrieve current number in control info sequence.
  curr_num = RD16_LE(&ndm->main_buf[current_location]);

  // If there's no next control page according to header, return -1.
  if (curr_num >= ndm->ctrl_pages)
    return (ui32)FsError2(NDM_BAD_META_DATA, EINVAL);

  // Look for page in same block first.
  for (p = curr_p + 1; p % ndm->pgs_per_blk; ++p) {
    page_status = is_next_ctrl_page(ndm, p, curr_num);
    if (page_status == NDM_CTRL_BLOCK)
      return p;
    else if (page_status == -1)
      return (ui32)-1;
  }

  // Get first and last page numbers in opposing control block.
  if (curr_p / ndm->pgs_per_blk == ndm->ctrl_blk0)
    p = ndm->ctrl_blk1 * ndm->pgs_per_blk;
  else
    p = ndm->ctrl_blk0 * ndm->pgs_per_blk;
  p_end = p + ndm->pgs_per_blk - 1;

  // Search second control block for next control page.
  do {
    page_status = is_next_ctrl_page(ndm, p, curr_num);
    if (page_status == NDM_CTRL_BLOCK)
      return p;
    else if (page_status == -1)
      return (ui32)-1;
  } while (++p <= p_end);

  // At this point, no next page can be found.
  return (ui32)FsError2(NDM_BAD_META_DATA, EINVAL);
}

// check_next_read: If next read spans control pages, adjust the
//                  current control information pointers
//
//       Input: ndm = pointer to NDM control block
//   In/Output:curr_loc = current location in control page
//   In/Output:pn = current control page
//   In/Output:ctrl_pages = control pages read so far
//       Input:  size = size of next read in bytes
//
//     Returns: 0 on success, -1 on failure
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static int check_next_read(CNDM ndm, ui32* curr_loc, ui32* pn, ui32* ctrl_pages, ui32 size) {
  // If next read goes past end of current control page, read next.
  if (*curr_loc + size > ndm->page_size) {
    // Figure out where the next page is.
    *pn = get_next_ctrl_page(ndm, *pn);
    if (*pn == (ui32)-1)
      return -1;

    // Reset current control location to beginning of the next page.
    *curr_loc = ndmGetHeaderControlDataStart(ndm);
    *ctrl_pages += 1;

    // Read the next page. Return -1 if error (ECC or fatal).
    if (ndm->read_page(*pn, ndm->main_buf, ndm->spare_buf, ndm->dev) < 0)
      return FsError2(NDM_EIO, EIO);

#if NDM_DEBUG
    ndm->logger.debug("read_ctrl: READ page #%u", *pn);
#endif
  }

  // Return success.
  return 0;
}

// read_ctrl_info: Read the NDM control information
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
static int read_ctrl_info(NDM ndm) {
  ui32 bn, vbn, i, curr_loc = CTRL_DATA_START, ctrl_pages = 1;
  ui32 p = ndm->frst_ctrl_page;
  ui16 major_version = RD16_LE(&ndm->main_buf[0]);

  // Shift header data for version 2.
  if (major_version != 1) {
    ndm->version_2 = TRUE;
    curr_loc += HDR_V2_SHIFT;
  }

  // Read the first control page. Return -1 if error (ECC or fatal).
  if (ndm->read_page(p, ndm->main_buf, ndm->spare_buf, ndm->dev) < 0)
    return FsError2(NDM_EIO, EIO);
#if NDM_DEBUG
  ndm->logger.debug("read_ctrl: READ page #%u", p);
#endif

  // Ensure the number of blocks and block size are correct.
  if (ndm->num_dev_blks != RD32_LE(&ndm->main_buf[curr_loc]))
    return FsError2(NDM_BAD_META_DATA, EINVAL);
  curr_loc += sizeof(ui32);
  if (ndm->block_size != RD32_LE(&ndm->main_buf[curr_loc]))
    return FsError2(NDM_BAD_META_DATA, EINVAL);
  curr_loc += sizeof(ui32);

  // Retrieve the control block pointers.
  ndm->ctrl_blk0 = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);
  ndm->ctrl_blk1 = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Retrieve free_virt_blk pointer.
  ndm->free_virt_blk = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Retrieve free_ctrl_blk pointer.
  ndm->free_ctrl_blk = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

#if NDM_DEBUG
  ndm->logger.debug("read_ctrl info:");
  ndm->logger.debug("  -> ctrl_seq    = %u", ndm->ctrl_seq);
  ndm->logger.debug("  -> ctrl_blk0   = %u", ndm->ctrl_blk0);
  ndm->logger.debug("  -> ctrl_blk1   = %u", ndm->ctrl_blk1);
  ndm->logger.debug("  -> free_virt_blk = %u", ndm->free_virt_blk);
  ndm->logger.debug("  -> free_ctrl_blk = %u", ndm->free_ctrl_blk);
#endif

  // Retrieve the transfer to block (if any).
  ndm->xfr_tblk = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Up to this point, versions 1 and 2 of the header match. Transfer info is not
  // optional for version 2 anymore.

  // If a bad block was being transferred, retrieve all other relevant
  // information about it so that the transfer can be redone. These fields are
  // not optional anymore after version 1.
  if (ndm->xfr_tblk != (ui32)-1 || major_version != 1) {
    // Retrieve the physical bad block being transferred.
    ndm->xfr_fblk = RD32_LE(&ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    // Retrieve the page offset of bad page in bad block.
    ndm->xfr_bad_po = RD32_LE(&ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    if (major_version == 1) {
      // Skip obsolete full/partial transfer flag.
      ++curr_loc;
    }

#if NDM_DEBUG
    ndm->logger.debug("  -> xfr_tblk       = %u", ndm->xfr_tblk);
    ndm->logger.debug("  -> xfr_fblk       = %u", ndm->xfr_fblk);
    ndm->logger.debug("  -> xfr_bad_po     = %u", ndm->xfr_bad_po);
#endif
  }

#if NDM_DEBUG
  if (ndm->xfr_tblk == (ui32)-1) {
    ndm->logger.debug("  -> xfr_tblk       = -1");
  }
#endif

  // Retrieve the number of partitions.
  ndm->num_partitions = RD32_LE(&ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);
#if NDM_DEBUG
  ndm->logger.debug("  -> num_partitions = %u", ndm->num_partitions);
  ndm->logger.debug("read_ctrl: init_bad_blk[]");
#endif

  // Retrieve the initial bad blocks map.
  for (ndm->num_bad_blks = i = 0;; ++i) {
    // If too many initial bad blocks, error.
    if (ndm->num_bad_blks > ndm->max_bad_blks)
      return FsError2(NDM_TOO_MANY_IBAD, EINVAL);

    // If next read spans control pages, adjust. Return -1 if error.
    if (check_next_read(ndm, &curr_loc, &p, &ctrl_pages, sizeof(ui32)))
      return -1;

    // Retrieve the next initial bad block.
    bn = RD32_LE(&ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

#if NDM_DEBUG
    ndm->logger.debug("    [%u] = %u", i, bn);
#endif

    // Store block in initial bad block map and check for end of map.
    ndm->init_bad_blk[i] = bn;
    if (bn == ndm->num_dev_blks)
      break;

    // Adjust running count of bad blocks to account for this one.
    ++ndm->num_bad_blks;
  }

#if NDM_DEBUG
  ndm->logger.debug("read_ctrl: run_bad_blk[]");
#endif

  // Retrieve running bad blocks map.
  for (ndm->num_rbb = 0;; ++ndm->num_rbb) {
    // If too many bad blocks, error.
    if (ndm->num_bad_blks > ndm->max_bad_blks)
      return FsError2(NDM_TOO_MANY_RBAD, EINVAL);

    // If next read spans control pages, adjust. Return -1 if error.
    if (check_next_read(ndm, &curr_loc, &p, &ctrl_pages, 2 * sizeof(ui32)))
      return -1;

    // Retrieve the next running bad block pair.
    vbn = RD32_LE(&ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);
    bn = RD32_LE(&ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    // If end of running blocks reached, stop.
    if (vbn == (ui32)-1 && bn == (ui32)-1)
      break;

    // Store bad block pair in running block map.
    ndm->run_bad_blk[ndm->num_rbb].key = vbn;
    ndm->run_bad_blk[ndm->num_rbb].val = bn;

#if NDM_DEBUG
    ndm->logger.debug("    [%u] key = %u, val = %u\n", ndm->num_rbb, vbn, bn);
#endif

    // Adjust running count of bad blocks to account for this one.
    ++ndm->num_bad_blks;
  }

  // Retrieve the NDM partitions if any.
  if (ndm->num_partitions) {
    size_t user_data_size = 0;
    size_t partition_size = sizeof(NDMPartition);
    PfAssert(ndm->partitions == NULL);
    if (ndm->version_2) {
      // Read the size of the first partition, and assume that's the size of
      // every partition. We can adjust this when we start using more than one
      // partition.
      PfAssert(ndm->num_partitions == 1);
      user_data_size = RD32_LE(&ndm->main_buf[curr_loc + sizeof(NDMPartition)]);
      partition_size += user_data_size + sizeof(ui32);
    }
    ndm->partitions = FsCalloc(ndm->num_partitions, partition_size);
    if (ndm->partitions == NULL)
      return -1;

#if NDM_DEBUG
    ndm->logger.debug("read_ctrl: partitions[]");
#endif

    // Read partitions from the control information one at a time.
    for (i = 0; i < ndm->num_partitions; ++i) {
#if NDM_PART_USER
      ui32 j;
#endif

      // If next read spans control pages, adjust. Return -1 if error.
      if (check_next_read(ndm, &curr_loc, &p, &ctrl_pages, partition_size))
        return -1;

      // Retrieve the next partition. Read partition first block.
      ndm->partitions[i].first_block = RD32_LE(&ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);

      // Read partition number of blocks.
      ndm->partitions[i].num_blocks = RD32_LE(&ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);

#if NDM_PART_USER
      // Read the user defined ui32s.
      for (j = 0; j < NDM_PART_USER; ++j) {
        ndm->partitions[i].user[j] = RD32_LE(&ndm->main_buf[curr_loc]);
        curr_loc += sizeof(ui32);
      }
#endif

      // Read the partition name.
      strncpy(ndm->partitions[i].name, (char*)&ndm->main_buf[curr_loc], NDM_PART_NAME_LEN - 1);
      curr_loc += NDM_PART_NAME_LEN;

      // Read the partition type.
      ndm->partitions[i].type = ndm->main_buf[curr_loc++];

      if (ndm->version_2) {
        // Attach the user data at the end of this partition data.
        PfAssert(RD32_LE(&ndm->main_buf[curr_loc]) <= user_data_size);
        curr_loc += sizeof(ui32);
        NDMPartitionInfo* info = (NDMPartitionInfo*)(ndm->partitions);
        info->user_data.data_size = user_data_size;
        if (user_data_size) {
          memcpy(info->user_data.data, &ndm->main_buf[curr_loc], user_data_size);
        }
        curr_loc += user_data_size;
      }

#if NDM_DEBUG
      ndm->logger.debug(" partition[%2u]:", i);
      ndm->logger.debug("   - name        = %s", ndm->partitions[i].name);
      ndm->logger.debug("   - first block = %u", ndm->partitions[i].first_block);
      ndm->logger.debug("   - num blocks  = %u", ndm->partitions[i].num_blocks);
#if NDM_PART_USER
      for (j = 0; j < NDM_PART_USER; ++j)
        ndm->logger.debug("   - user[%u]     = %u", j, ndm->partitions[i].user[j]);
#endif
#endif  // NDM_DEBUG
    }
  }

  // Check that number of read pages agrees with recorded one.
  if (ctrl_pages != ndm->ctrl_pages || p != ndm->last_ctrl_page)
    return FsError2(NDM_BAD_META_DATA, EINVAL);

  // Return success.
  return 0;
}

// recover_bad_blk: Recover from an unexpected power down while a bad
//              block was being transferred
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
static int recover_bad_blk(NDM ndm) {
  ui32 i, bpn;
  int rc;

  // Ensure the 'transfer from' block value is valid.
  if (ndm->xfr_fblk >= ndm->num_dev_blks) {
    ndm->logger.error("Failed to recover NDM Bad Block. Invalid |transfer_from| block %u.",
                      ndm->xfr_fblk);
    return FsError2(NDM_BAD_META_DATA, EINVAL);
  }

  // Ensure the 'transfer to' block value is valid.
  if (ndm->xfr_tblk < ndm->frst_reserved || ndm->xfr_tblk >= ndm->free_virt_blk) {
    ndm->logger.error("Failed to recover NDM Bad Block. Invalid |transfer_to| block %u.",
                      ndm->xfr_tblk);
    return FsError2(NDM_BAD_META_DATA, EINVAL);
  }

  // Return error if doing a read-only initialization.
  if (ndm->flags & FSF_READ_ONLY_INIT) {
    ndm->logger.error("Failed to recover NDM Bad Block. NDM in read-only mode.");
    return FsError2(NDM_BAD_BLK_RECOV, EINVAL);
  }

  // Erase the 'transfer to' block. Return if fatal error.
  rc = ndm->erase_block(ndm->xfr_tblk * ndm->pgs_per_blk, ndm->dev);
  if (rc == -2) {
    ndm->logger.error("Failed to recover NDM Bad Block. Failed to erase |transfer_to_block|.");
    return FsError2(NDM_EIO, EIO);
  }

  // Check if block erase command failed.
  if (rc < 0) {
    // Adjust bad block count. If too many, error.
    if (++ndm->num_bad_blks > ndm->max_bad_blks)
      return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

    // Find running list entry with this 'transfer from/to' block pair.
    for (i = 0;; ++i) {
      if (i == ndm->num_rbb) {
        ndm->logger.error(
            "Failed to recover NDM Bad Block. Failed to obtain run bad block for "
            "|transfer_to/from| block.");
        return FsError2(NDM_ASSERT, EINVAL);
      }
      if (ndm->run_bad_blk[i].key == ndm->xfr_fblk && ndm->run_bad_blk[i].val == ndm->xfr_tblk)
        break;
    }

    // Invalidate the 'transfer to' block since it's bad now.
    ndm->run_bad_blk[i].val = (ui32)-1;

    // Add new bad block list entry with this 'transfer to' block.
    ndm->run_bad_blk[ndm->num_rbb].key = ndm->xfr_tblk;
    ndm->run_bad_blk[ndm->num_rbb].val = (ui32)-1;
    ++ndm->num_rbb;
  }

  // Else erase was successful. Adjust free block pointer.
  else {
    PfAssert(ndm->free_virt_blk == (ui32)-1 || ndm->xfr_tblk + 1 == ndm->free_virt_blk);
    ndm->free_virt_blk = ndm->xfr_tblk;
  }

  // Reset 'transfer to' block pointer.
  ndm->xfr_tblk = (ui32)-1;

  // Figure out bad page number on bad block.
  bpn = ndm->xfr_fblk * ndm->pgs_per_blk + ndm->xfr_bad_po;

  // Mark block bad and do bad block recovery for write failure.
  return ndmMarkBadBlock(ndm, bpn, WRITE_PAGE);
}

//    init_ndm: Initialize an NDM by reading the flash
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
static int init_ndm(NDM ndm) {
  int wr_metadata;
  ui32 ctrl_blk;

  // See if device is formatted with NDM metadata. Check for error.
  if (format_status(ndm) != 0) {
    ndm->logger.info("No NDM control block found.");
    // If no metadata was found and initialization is not being done
    // in read-only mode, format the device. Else return -1.
    if ((GetFsErrCode() == NDM_NO_META_BLK) && FLAG_IS_CLR(ndm->flags, FSF_READ_ONLY_INIT)) {
      ndm->logger.info("No meta block found. Initializing NDM Volume.");
      return ndm_format(ndm);
    }
    return -1;
  }

  // Else device is NDM formatted. Find latest control information.
  if (find_last_ctrl_info(ndm)) {
    ndm->logger.warning("Failed to obtain valid NDM Control Block.");
    return -1;
  }

  // Read the control information. Return -1 if error.
  PfAssert(ndm->ctrl_blk1 == (ui32)-1);
  if (read_ctrl_info(ndm)) {
    ndm->logger.info("Failed to read contents NDM Control Block.");
    return -1;
  }

  // Set flag if configured to write metadata at every startup to
  // ensure control block reads don't create read-disturb errors.
  wr_metadata = FLAG_IS_SET(ndm->flags, FSF_NDM_INIT_WRITE);

  // Match the block the control information was found on with either
  // ctrl_blk0 or ctrl_blk1, pick other block for next control write.
  ctrl_blk = ndm->last_ctrl_page / ndm->pgs_per_blk;
  if (ctrl_blk == ndm->ctrl_blk0)
    ctrl_blk = ndm->ctrl_blk1;
  else if (ctrl_blk == ndm->ctrl_blk1)
    ctrl_blk = ndm->ctrl_blk0;

  // Else this must be the first start from a preprogrammed image.
  else {
    // Fail start-up if image's running bad block count is non-zero.
    if (ndm->num_rbb) {
      ndm->logger.error("Failed to initialize NDM. |num_rbb| must be zero, found %d", ndm->num_rbb);
      return FsError2(NDM_IMAGE_RBB_CNT, ENXIO);
    }

    // Redo the initial bad block list for our device.
    if (init_ibad_list(ndm)) {
      ndm->logger.error("Failed to initialize NDM bad block list.");
      return -1;
    }

    // Request first metadata write and have it on ctrl_blk0.
    ctrl_blk = ndm->ctrl_blk0;
    wr_metadata = TRUE;
  }

  // Assign starting control write page number.
  ndm->next_ctrl_start = ctrl_blk * ndm->pgs_per_blk;

  // Compute the cutoff between virtual blocks and reserved blocks.
  set_frst_ndm_block(ndm);

  // Ensure even lowest running bad block lies in reserved area.
  if (ndm->run_bad_blk[0].val < ndm->frst_reserved) {
    ndm->logger.error(
        "Failed to initialize NDM. First bad block in unexpected location. First bad block at %d, "
        "reservation starts at %d.",
        ndm->run_bad_blk[0].val, ndm->frst_reserved);
    return FsError2(NDM_RBAD_LOCATION, EINVAL);
  }

  // If in the middle of transferring a bad block, continue transfer.
  if (ndm->xfr_tblk != (ui32)-1) {
    ndm->logger.info("Resuming bad block transfer.");
    return recover_bad_blk(ndm);
  }

  // Check if NDM metadata write is requested or needed.
  if (wr_metadata) {
    // Return error if doing a read-only initialization.
    if (ndm->flags & FSF_READ_ONLY_INIT) {
      ndm->logger.info("Failed to Initialize NDM, attempted to write metadata on READ-ONLY mode.");
      return FsError2(NDM_META_WR_REQ, EINVAL);
    }

    // Write initial control information and return status.
    ndm->xfr_tblk = (ui32)-1;
    return ndmWrCtrl(ndm);
  }

  // Return success.
  return 0;
}

// ndm_xfr_page: Substitute if driver does not supply transfer_page()
//
//     Inputs: old_pn = old page number
//             new_pn = new page number
//             buf = pointer to temperary buffer for page data
//             old_spare = buffer to hold old page spare contents
//             new_spare = buffer to hold new page spare contents
//             encode_spare = flag to encode/not encode spare bytes
//                            1 through 14 (FTL encodes/FFS does not)
//             ndm_ptr = pointer to TargetNDM control block
//
//    Returns: 0 on success, -2 on fatal error, -1 on chip error,
//             1 on ECC decode error
//
static int ndm_xfr_page(ui32 old_pn, ui32 new_pn, ui8* buf, ui8* old_spare, ui8* new_spare,
                        int encode_spare, void* ndm_ptr) {
  int status;
  NDM ndm = ndm_ptr;

  // Read page data. Return is 1, 0, -2, or -1.
  status = ndm->read_page(old_pn, buf, old_spare, ndm->dev);

  // Error check: return 1 for ECC decode error, else -2 fatal error.
  if (status < 0) {
    if (status == -1) {
      ndm->logger.error("Failed to read page %d. ECC decode error.", old_pn);
      FsError2(NDM_RD_ECC_FAIL, EIO);
      return 1;
    }
    ndm->logger.error("Failed to read page %d. IO Error.");
    FsError2(NDM_EIO, EIO);
    return -2;
  }

  // Write data page to flash and return status.
  return ndm->write_page(new_pn, buf, new_spare, encode_spare, ndm->dev);
}

// Global Function Definitions

//   NdmInit: Initialize NDM
//
//     Returns: 0 on success, -1 on failure
//
int NdmInit(void) {
  // Initialize the devices list.
  CIRC_LIST_INIT(&NdmDevs);

  // Create the NDM global synchronization semaphore.
  NdmSem = semCreate("NDM_SEM", 1, OS_FIFO);
  if (NdmSem == NULL) {
    FsError2(NDM_SEM_CRE_ERR, errno);
    return -1;
  }

  // Return success.
  return 0;
}

//   ndmAddDev: Create a new NDM
//
//       Input: dvr = pointer to NDM driver control block
//
//     Returns: New NDM control block on success, NULL on error
//
NDM ndmAddDev(const NDMDrvr* dvr) {
  char sem_name[9];
  uint eb_alloc_sz;
  NDM ndm;

#if NV_NDM_CTRL_STORE
  // Can only use one NDM device with NVRAM control page storage.
  if (NdmDevCnt > 0) {
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }
#endif

  // Error if unsupported flash type.
#if INC_FTL_NDM_SLC
  if (dvr->type != NDM_SLC) {
#else
  if (dvr->type != NDM_MLC) {
#endif
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Ensure NDM driver flags are valid.
  if (dvr->flags & ~(FSF_MULTI_ACCESS | FSF_TRANSFER_PAGE | FSF_FREE_SPARE_ECC |
                     FSF_NDM_INIT_WRITE | FSF_READ_ONLY_INIT)) {
    dvr->logger.error("Failed to initialized NDM. Invalid flag.");
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Check for valid number of blocks.
  if (dvr->num_blocks <= dvr->max_bad_blocks + NDM_META_BLKS) {
    dvr->logger.error(
        "Failed to initialized NDM. Not enough blocks for reservation and control blocks, found %d "
        "but required %d.",
        dvr->num_blocks, (dvr->max_bad_blocks + NDM_META_BLKS));
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Check for valid page size (multiple of 512).
  if (dvr->page_size == 0 || dvr->page_size % 512) {
    dvr->logger.error(
        "Failed to initialized NDM. Invalid page size, must positive multiple of 512, but found "
        "%d.",
        dvr->page_size);
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Check for valid spare bytes size.
  if (dvr->eb_size > dvr->page_size || dvr->eb_size < 16) {
    dvr->logger.error(
        "Failed to initialized NDM. Invalid page oob size, must at least 16 bytes, but found %d.",
        dvr->eb_size);
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Allocate space for TargetNDM control block.
  ndm = FsCalloc(1, sizeof(struct ndm));
  if (ndm == NULL) {
    dvr->logger.error("Failed to initialized NDM. Memory allocation failed.");
    FsError2(NDM_ENOMEM, ENOMEM);
    return NULL;
  }

  // Set the number of virtual blocks.
  ndm->num_vblks = dvr->num_blocks - dvr->max_bad_blocks - NDM_META_BLKS;

  // Set the other driver dependent variables.
  ndm->num_dev_blks = dvr->num_blocks;
  ndm->max_bad_blks = dvr->max_bad_blocks;
  ndm->block_size = dvr->block_size;
  ndm->page_size = dvr->page_size;
  ndm->eb_size = dvr->eb_size;
  ndm->pgs_per_blk = ndm->block_size / ndm->page_size;
  ndm->flags = dvr->flags;
  ndm->logger = dvr->logger;

  // Allocate memory for initial and running bad blocks arrays.
  ndm->init_bad_blk = FsMalloc((ndm->max_bad_blks + 1) * sizeof(ui32));
  if (ndm->init_bad_blk == NULL) {
    ndm->logger.error(
        "Failed to initialize NDM. Failed to allocated memory for factory bad block table.");
    FsError2(NDM_ENOMEM, ENOMEM);
    goto ndmAddDe_err;
  }
  ndm->run_bad_blk = FsMalloc((ndm->max_bad_blks + 1) * sizeof(Pair));
  if (ndm->run_bad_blk == NULL) {
    ndm->logger.error(
        "Failed to initialized NDM. Failed to allocated memory for runtime bad block table.");
    FsError2(NDM_ENOMEM, ENOMEM);
    goto ndmAddDe_err;
  }

  // Initialize the initial and running bad block arrays.
  memset(ndm->init_bad_blk, 0xFF, (ndm->max_bad_blks + 1) * sizeof(ui32));
  memset(ndm->run_bad_blk, 0xFF, (ndm->max_bad_blks + 1) * sizeof(Pair));

  // Create the access semaphore.
  snprintf(sem_name, sizeof(sem_name), "NDM_S%03d", NdmSemCount++);
  ndm->sem = semCreate(sem_name, 1, OS_FIFO);
  if (ndm->sem == NULL) {
    ndm->logger.error("Failed to initialize NDM. Failed to created semaphore.");
    FsError2(NDM_SEM_CRE_ERR, errno);
    goto ndmAddDe_err;
  }

  // Ensure spare area buffers allocated by NDM are cache aligned.
  eb_alloc_sz = ndm->eb_size;
#if CACHE_LINE_SIZE
  eb_alloc_sz = ((eb_alloc_sz + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE;
#endif

  // Allocate memory for page main and spare data buffers.
  ndm->main_buf = FsAalloc(ndm->page_size + 2 * eb_alloc_sz);
  if (ndm->main_buf == NULL) {
    ndm->logger.error(
        "Failed to initialied NDM. Failed to allocate memory for page content staging buffer.");
    FsError2(NDM_ENOMEM, ENOMEM);
    goto ndmAddDe_err;
  }
  ndm->spare_buf = ndm->main_buf + ndm->page_size;
  ndm->tmp_spare = ndm->spare_buf + eb_alloc_sz;

  // Initialize all other NDM variables.
  ndm->ctrl_blk0 = ndm->ctrl_blk1 = (ui32)-1;
  ndm->frst_ctrl_page = ndm->last_ctrl_page = (ui32)-1;
  ndm->free_virt_blk = ndm->free_ctrl_blk = (ui32)-1;
  ndm->ctrl_seq = (ui32)-1;
  ndm->xfr_tblk = ndm->xfr_fblk = ndm->xfr_bad_po = (ui32)-1;
  ndm->last_wr_vbn = ndm->last_wr_pbn = (ui32)-1;
  ndm->last_rd_vbn = ndm->last_rd_pbn = (ui32)-1;
  ndm->format_with_v2 = dvr->format_version_2;

  // Install driver callback routine function pointers.
  ndm->write_page = dvr->write_data_and_spare;
  ndm->read_page = dvr->read_decode_data;
  ndm->read_decode_spare = dvr->read_decode_spare;
  ndm->read_spare = dvr->read_spare;
  ndm->page_blank = dvr->data_and_spare_erased;
  ndm->check_page = dvr->data_and_spare_check;
  ndm->erase_block = dvr->erase_block;
  ndm->is_block_bad = dvr->is_block_bad;
  ndm->dev = dvr->dev;

#if INC_FTL_NDM_MLC
  // The 'pair_offset' driver function does not take a page/address.
  ndm->pair_offset = dvr->pair_offset;
#endif

  // If driver supplies transfer page function, use it.
  if (FLAG_IS_SET(dvr->flags, FSF_TRANSFER_PAGE)) {
    ndm->logger.info("Using driver page transfer routine.");
    ndm->dev_ndm = ndm->dev;
    ndm->xfr_page = dvr->transfer_page;

    // Else use internal read-page/write-page substitute.
  } else {
    ndm->logger.info("Using software page transfer routine.");
    ndm->dev_ndm = ndm;
    ndm->xfr_page = ndm_xfr_page;
  }

  // If driver read/write pages supplied, use them directly.
  if (FLAG_IS_SET(dvr->flags, FSF_MULTI_ACCESS)) {
    ndm->read_pages = dvr->read_pages;
    ndm->write_pages = dvr->write_pages;
  }

  // Initialize the NDM.
  if (init_ndm(ndm)) {
    ndm->logger.error("Failed to initialized NDM layer.");
    goto ndmAddDe_err;
  }

#if NV_NDM_CTRL_STORE
  // Account for NVRAM routine use.
  ++NdmDevCnt;
#endif

  // Add NDM to global NDM list while holding access semaphore.
  semPend(NdmSem, WAIT_FOREVER);
  CIRC_LIST_APPEND(&ndm->link, &NdmDevs);
  semPostBin(NdmSem);

  // Success! Returns handle to new NDM control block.
  return ndm;

// Error exit.
ndmAddDe_err:
  if (ndm->init_bad_blk)
    FsFree(ndm->init_bad_blk);
  if (ndm->run_bad_blk)
    FsFree(ndm->run_bad_blk);
  if (ndm->sem)
    semDelete(&ndm->sem);
  if (ndm->main_buf)
    FsAfreeClear(&ndm->main_buf);
  if (ndm->partitions)
    FsFree(ndm->partitions);
  FsFree(ndm);
  return NULL;
}

//   ndmDelDev: Delete (uninitialize) the NDM
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
int ndmDelDev(NDM ndm) {
  CircLink* circ;
  int saved_errno;

  // Acquire exclusive access to global NDM semaphore.
  semPend(NdmSem, WAIT_FOREVER);
  ndm->logger.info("Removing NDM device.");

  // Ensure the device is on the device list.
  for (circ = CIRC_LIST_HEAD(&NdmDevs);; circ = circ->next_bck) {
    // If the device was not found, return error.
    if (CIRC_LIST_AT_END(circ, &NdmDevs)) {
      ndm->logger.info("failed to remove ndm device. device not found.");
      semPostBin(NdmSem);
      return FsError2(NDM_NOT_FOUND, ENOENT);
    }

    // If device found, stop looking.
    if (ndm == (void*)circ)
      break;
  }

  // Remove device from list of devices.
  CIRC_NODE_REMOVE(&ndm->link);
  CIRC_NODE_INIT(&ndm->link);

  // Release exclusive access to global NDM semaphore.
  semPostBin(NdmSem);

  // Remove all volumes from device.
  saved_errno = errno;
  (void)ndmDelVols(ndm);
  errno = saved_errno;

  // Free the initial and running bad block maps.
  FsFree(ndm->init_bad_blk);
  FsFree(ndm->run_bad_blk);

  // Free the partitions table.
  if (ndm->partitions)
    FsFree(ndm->partitions);

  // Free the region semaphore, temporary space, and control block.
  semDelete(&ndm->sem);
  FsAfreeClear(&ndm->main_buf);
  FsFree(ndm);

#if NV_NDM_CTRL_STORE
  // Decrement NDM device count.
  --NdmDevCnt;
#endif

  // Return success.
  return 0;
}

// ndmInitBadBlock: Check if a block is in initial bad block map
//
//      Inputs: ndm = pointer to NDM control block
//              b = block to check
//
//     Returns: TRUE iff initial bad block, FALSE otherwise
//
int ndmInitBadBlock(CNDM ndm, ui32 b) {
  ui32 i;

  // Loop over list of initially bad blocks.
  for (i = 0; i <= ndm->max_bad_blks; ++i) {
    // If end of map, stop.
    if (ndm->init_bad_blk[i] == ndm->num_dev_blks)
      break;

    // If block found in map, it's initial bad block.
    if (ndm->init_bad_blk[i] == b)
      return TRUE;
  }

  // Return FALSE. Block is not an initial bad block.
  return FALSE;
}

#if RDBACK_CHECK
//   ndmCkMeta: Read-back verify the TargetNDM metadata
//
//       Input: ndm0 = pointer to NDM control block
//
void ndmCkMeta(NDM ndm0) {
  int rc;
  NDM ndm1;

  // Allocate TargetNDM control block for metadata comparison.
  ndm1 = FsCalloc(1, sizeof(struct ndm));
  PfAssert(ndm1 != NULL);

  // Copy initialized (not read) structure values.
  memcpy(ndm1, ndm0, sizeof(struct ndm));

  // Read the latest control info into temporary control block, using
  // - if any - previously allocated partition memory.
  rc = read_ctrl_info(ndm1);
  PfAssert(rc == 0);

  // Compare control block used for writing with one used for reading.
  rc = memcmp(ndm1, ndm0, sizeof(struct ndm));
  PfAssert(rc == 0);

  // Free allocated TargetNDM test control block.
  free(ndm1);
}
#endif  // RDBACK_CHECK
