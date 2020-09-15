// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ndmp.h"

// Configuration
#define BBL_INSERT_INC TRUE
#define BBL_INSERT_DEBUG FALSE

// Symbol Definitions

//
// Reason for get_pbn() virtual to physical block mapping
//
#define WR_MAPPING 0
#define RD_MAPPING 1

// Global Data Definitions
#if BBL_INSERT_INC
static ui32 ExtractedCnt;
static Pair* ExtractedList;
#endif

// Local Function Definitions

#if BBL_INSERT_DEBUG
//   show_rbbl: Show NDM's running bad block list
//
//      Inputs: list = pointer to NDM control block running BB list
//              cnt = number of bad blocks in list
//
static void show_rbbl(NDM ndm, Pair* list, ui32 cnt) {
  Pair *past_end, *pair = list;

  for (past_end = pair + cnt; pair < past_end; ++pair)
    ndm->logger.info("pair %u: vblk/key=%u, pblk/val=%d\n", pair - list, pair->key, pair->val);
}
#endif

// get_ctrl_size: Counts the number of pages needed to write current
//              control information.
//
//       Input: ndm = pointer to NDM control block.
//
//     Returns: control info size in pages on success, 0 on failure.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static int get_ctrl_size(CNDM ndm) {
  ui32 i, curr_loc, num_pages = 0;

  const ui32 control_data_start = ndmGetHeaderControlDataStart(ndm);

  // Figure out how many control pages are needed. Each control page
  // has a header (CTRL_DATA_START bytes). Control information has the
  // following preamble:
  //   - device number of blocks + block size (8 bytes = 2 ui32s)
  //   - control block pointers (2)           (8 bytes = 2 ui32s)
  //   - free block/ctrl block pointers       (8 bytes = 2 ui32s)
  //   - number of partitions                 (4 bytes = 1 ui32)
  //   - on normal write
  //       - invalid transfer to block        (4 bytes = 1 ui32)
  //   - on bad block transfer
  //       - transfer to block                (4 bytes = 1 ui32)
  //       - transferred block                (4 bytes = 1 ui32)
  //       - bad page in transferred block    (4 bytes = 1 ui32)
  //       - partial/full scan flag           (1 byte)
  curr_loc = control_data_start;
  curr_loc += 8 * sizeof(ui32);
  if (ndm->xfr_tblk != (ui32)-1)
    curr_loc += 2 * sizeof(ui32) + 1;

  // After the preamble, the initial bad block map is written. Figure
  // out how much space it takes.
  for (i = 0;; ++i) {
    // Ensure the initial bad block map is valid.
    if (i > ndm->max_bad_blks) {
      FsError2(NDM_ASSERT, EINVAL);
      return 0;
    }

    // Add current entry.
    if (curr_loc + sizeof(ui32) > ndm->page_size) {
      ++num_pages;
      curr_loc = control_data_start;
    }
    curr_loc += sizeof(ui32);

    // If we've reached end of initial bad block map, stop.
    if (ndm->init_bad_blk[i] == ndm->num_dev_blks)
      break;
  }

  // The running bad block map is written next. Determine its size.
  for (i = 0;; ++i) {
    // Ensure running bad block map is valid.
    if (i > ndm->max_bad_blks) {
      FsError2(NDM_ASSERT, EINVAL);
      return 0;
    }

    // Add current entry.
    if (curr_loc + 2 * sizeof(ui32) > ndm->page_size) {
      ++num_pages;
      curr_loc = control_data_start;
    }
    curr_loc += 2 * sizeof(ui32);

    // If we've reached the end of running bad block map, stop.
    if (i == ndm->num_rbb) {
      if (curr_loc + 2 * sizeof(ui32) > ndm->page_size) {
        ++num_pages;
        curr_loc = control_data_start;
      }
      curr_loc += 2 * sizeof(ui32);
      break;
    }
  }

  // The partitions are written. Figure out how much space they take.
  for (i = 0; i < ndm->num_partitions; ++i) {
    if (curr_loc + sizeof(NDMPartition) > ndm->page_size) {
      ++num_pages;
      curr_loc = control_data_start;
    }
    curr_loc += sizeof(NDMPartition);
  }

  // If last control page will be partially written, account for it.
  if (curr_loc > control_data_start)
    ++num_pages;

  // Output number of control pages and return success.
  return num_pages;
}

// write_crc: Writes a new crc to an NDM control block.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Preconditions: Header version 1.1.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static void write_crc(CNDM ndm) {
  ui32 crc = CRC32_START, i;

  ui32 crc_location = HDR_CRC_LOC;
  const ui32 data_start = ndmGetHeaderControlDataStart(ndm);
  if (ndm->version_2) {
    crc_location += HDR_V2_SHIFT;
  }

  // First perform CRC on all but the 4 CRC bytes.
  for (i = 0; i < ndm->page_size; ++i) {
    if (i == crc_location) {
      i = data_start;
    }
    crc = CRC32_UPDATE(crc, ndm->main_buf[i]);
  }

  crc = ~crc;
  WR32_LE(crc, &ndm->main_buf[crc_location]);  // lint !e850
}

// ndmReadControlCrc: Calculates the crc of an NDM control block.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: page crc.
//
//     Preconditions: Header version 1.x or 2.x.
//
ui32 ndmReadControlCrc(CNDM ndm) {
  ui32 crc, i;
  ui32 crc_location = HDR_CRC_LOC;
  ui32 data_start = CTRL_DATA_START;

  // Shift header data for version 2.
  if (RD16_LE(&ndm->main_buf[0]) != 1) {
    crc_location += HDR_V2_SHIFT;
    data_start += HDR_V2_SHIFT;
  }

  // Perform CRC on page. The 4 CRC bytes are in the page header.
  // First perform CRC on all but the 4 CRC bytes.
  for (crc = CRC32_START, i = 0; i < ndm->page_size; ++i) {
    if (i == crc_location) {
      i = data_start;
    }
    crc = CRC32_UPDATE(crc, ndm->main_buf[i]);
  }

  // Now run the CRC bytes through the CRC encoding.
  for (i = crc_location; i < data_start; ++i) {  // lint !e850
    crc = CRC32_UPDATE(crc, ndm->main_buf[i]);
  }

  return crc;
}

// ndmGetHeaderCurrentLocation: Returns the location of |current_location|.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: Offset within the control header.
//
ui32 ndmGetHeaderCurrentLocation(CNDM ndm) {
  ui32 current_location = HDR_CURR_LOC;
  if (ndm->version_2) {
    current_location += HDR_V2_SHIFT;
  }
  return current_location;
}

// ndmGetHeaderLastLocation: Returns the location of |last_location|.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: Offset within the control header.
//
ui32 ndmGetHeaderLastLocation(CNDM ndm) {
  ui32 last_location = HDR_LAST_LOC;
  if (ndm->version_2) {
    last_location += HDR_V2_SHIFT;
  }
  return last_location;
}

// ndmGetHeaderSequenceLocation: Returns the location of |sequence_location|.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: Offset within the control header.
//
ui32 ndmGetHeaderSequenceLocation(CNDM ndm) {
  ui32 sequence_location = HDR_SEQ_LOC;
  if (ndm->version_2) {
    sequence_location += HDR_V2_SHIFT;
  }
  return sequence_location;
}

// ndmGetHeaderControlDataStart: Returns the location of |control_data_start|.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: Offset within the control header.
//
ui32 ndmGetHeaderControlDataStart(CNDM ndm) {
  ui32 control_data_start = CTRL_DATA_START;
  if (ndm->version_2) {
    control_data_start += HDR_V2_SHIFT;
  }
  return control_data_start;
}

// wr_ctrl_page: Writes a page of control information to flash.
//
//      Inputs: ndm = pointer to NDM control block.
//              cpc = current control page count.
//   In/Output: *curr_pnp = page number where next page is written.
//      Output: *badblkp = bad block number (if page write fails).
//
//     Returns: 0 if successful, -1 if block failed while writing,
//              -2 if fatal error.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static int wr_ctrl_page(NDM ndm, ui32 cpc, ui32* curr_pnp, ui32* badblkp) {
  ui32 cpn = *curr_pnp;
  int rc;

  const ui32 current_location = ndmGetHeaderCurrentLocation(ndm);
  if (ndm->version_2) {
    // Write version information.
    WR16_LE(2, &ndm->main_buf[0]);
    WR16_LE(0, &ndm->main_buf[sizeof(ui16)]);
  }

  // Fill current page count in header.
  WR16_LE(cpc, &ndm->main_buf[current_location]);

  write_crc(ndm);

  // Write page to flash. Check for error indication.
  rc = ndm->write_page(cpn, ndm->main_buf, ndm->spare_buf, NDM_ECC_VAL, ndm->dev);
  if (rc) {
    // If fatal error, return fatal error indication.
    if (rc == -2) {
      FsError2(NDM_EIO, EIO);
      return rc;

      // Else bad block caused write failure, output block number and
      // return bad block indication.
    } else {
      PfAssert(rc == -1);
#if NDM_DEBUG
      printf("wr_ctrl_page: bad block for #%u at page #%u\n", cpc, cpn);
#endif
      *badblkp = cpn / ndm->pgs_per_blk;
      return -1;
    }
  }

  // Update first and/or last control page.
  if (cpc == 1) {
    ndm->frst_ctrl_page = cpn;
#if NV_NDM_CTRL_STORE
    NvNdmCtrlPgWr(cpn);
#endif
  }
  if (cpc == ndm->ctrl_pages)
    ndm->last_ctrl_page = cpn;
#if NDM_DEBUG
  printf("wr_ctrl_page: wrote %u at page %u (block %u)\n", cpc, cpn, cpn / ndm->pgs_per_blk);
#endif

  // Advance to next page to write control information into.
  // Just increment page number if not on last page of block.
  if ((cpn + 1) % ndm->pgs_per_blk)
    ++cpn;

  // Else prepare to switch to new control page.
  else {
    // Switch to first page on opposing control block.
    if (cpn / ndm->pgs_per_blk == ndm->ctrl_blk0)
      cpn = ndm->ctrl_blk1 * ndm->pgs_per_blk;
    else
      cpn = ndm->ctrl_blk0 * ndm->pgs_per_blk;

    // Erase new block now, before its first write. Check for error.
    rc = ndm->erase_block(cpn, ndm->dev);
    if (rc) {
      // If fatal error, return fatal error indication.
      if (rc == -2) {
        FsError2(NDM_EIO, EIO);
        return rc;
      }

      // Else bad block caused erase failure, output block number and
      // return bad block indication.
      else {
        PfAssert(rc == -1);
#if NDM_DEBUG
        printf("wr_ctrl_page: bad block for #%u at page #%u\n", cpc, cpn);
#endif
        *badblkp = cpn / ndm->pgs_per_blk;
        return -1;
      }
    }
  }

  // Output page number for next control write and return success.
  *curr_pnp = cpn;
  return 0;
}

// wr_ctrl_info: Writes NDM control information.
//
//      Inputs: ndm = pointer to NDM control block.
//              frst_page = first page to write to.
//      Output: *badblkp = number of bad control block (if any).
//
//     Returns: 0 if successful, -1 if block failed, -2 if fatal error.
//
//     Preconditions: Header version 1.x or 2.x.
//                    ndm->version_2 initialized.
//
static int wr_ctrl_info(NDM ndm, ui32 frst_page, ui32* badblkp) {
  ui32 i, curr_loc, write_count = 1, cpn = frst_page;
  int status;

  // Determine control information size as number of pages.
  ndm->ctrl_pages = get_ctrl_size(ndm);
  if (ndm->ctrl_pages == 0)
    return -2;
#if NDM_DEBUG
  printf("wr_ctrl_inf: preparing to write %u NDM ctrl pages\n", ndm->ctrl_pages);
#endif

  // Initialize the spare area: 0xFF except for the signature bytes
  // and NDM control page mark.
  memset(ndm->spare_buf, 0xFF, ndm->eb_size);
  memcpy(&ndm->spare_buf[EB_FRST_RESERVED], CTRL_SIG, CTRL_SIG_SZ);
  ndm->spare_buf[EB_REG_MARK] = 0;

  // Initialize main page with 0xFF.
  memset(ndm->main_buf, 0xFF, ndm->page_size);

  // Set the constant part of the control page header: last location
  // and sequence number. The current location varies with page number.
  const ui32 last_location = ndmGetHeaderLastLocation(ndm);
  const ui32 sequence_location = ndmGetHeaderSequenceLocation(ndm);
  const ui32 control_data_start = ndmGetHeaderControlDataStart(ndm);

  WR16_LE(ndm->ctrl_pages, &ndm->main_buf[last_location]);
  ++ndm->ctrl_seq;
  WR32_LE(ndm->ctrl_seq, &ndm->main_buf[sequence_location]);

  // Set the first control page data, starting with the device size.
  curr_loc = control_data_start;
  WR32_LE(ndm->num_dev_blks, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);
  WR32_LE(ndm->block_size, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Write the control block numbers.
  WR32_LE(ndm->ctrl_blk0, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);
  WR32_LE(ndm->ctrl_blk1, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Add free_virt_blk pointer.
  WR32_LE(ndm->free_virt_blk, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // Add free_ctrl_blk number.
  WR32_LE(ndm->free_ctrl_blk, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

#if NDM_DEBUG
  puts("wr_ctrl_inf:");
  printf("  -> ctrl_seq    = %u\n", ndm->ctrl_seq);
  printf("  -> ctrl_blk0   = %u\n", ndm->ctrl_blk0);
  printf("  -> ctrl_blk1   = %u\n", ndm->ctrl_blk1);
  printf("  -> free_virt_blk = %u\n", ndm->free_virt_blk);
  printf("  -> free_ctrl_blk = %u\n", ndm->free_ctrl_blk);
#endif

  // Add 'transfer to' physical block number value/flag.
  WR32_LE(ndm->xfr_tblk, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);

  // If bad block transfer, add remaining bad block transfer info.
  if (ndm->xfr_tblk != (ui32)-1 || ndm->version_2) {
    // Add physical block number of the bad block being transferred.
    WR32_LE(ndm->xfr_fblk, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    // Add page offset of bad page in bad block.
    WR32_LE(ndm->xfr_bad_po, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    if (!ndm->version_2) {
      // Add (legacy) partial scan flag.
      ndm->main_buf[curr_loc++] = PARTIAL_SCAN;
    }

#if NDM_DEBUG
    printf("  -> xfr_tblk       = %u\n", ndm->xfr_tblk);
    printf("  -> xfr_fblk       = %u\n", ndm->xfr_fblk);
    printf("  -> xfr_bad_po     = %u\n", ndm->xfr_bad_po);
#endif
  }

#if NDM_DEBUG
  if (ndm->xfr_tblk == (ui32)-1) {
    puts("  -> xfr_tblk      = -1");
  }
#endif

  // Write the number of partitions.
  WR32_LE(ndm->num_partitions, &ndm->main_buf[curr_loc]);
  curr_loc += sizeof(ui32);
#if NDM_DEBUG
  printf("  -> num_partitions = %u\n", ndm->num_partitions);
#endif

  // Now write the initial bad block map.
  for (i = 0;; ++i) {
    // If next write would go past end of control page, write page.
    if (curr_loc + sizeof(ui32) > ndm->page_size) {
      status = wr_ctrl_page(ndm, write_count++, &cpn, badblkp);
      if (status)
        return status;
      curr_loc = control_data_start;
    }

    // Add next bad block.
    WR32_LE(ndm->init_bad_blk[i], &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    // If end of map is reached, stop.
    if (ndm->init_bad_blk[i] == ndm->num_dev_blks)
      break;

#if NDM_DEBUG
    printf("  -> init_bad_blk[%2u] = %u\n", i, ndm->init_bad_blk[i]);
#endif
  }

  // Now write the running bad block map.
  for (i = 0;; ++i) {
    // If next write would go past end of control page, write page.
    if (curr_loc + 2 * sizeof(ui32) > ndm->page_size) {
      status = wr_ctrl_page(ndm, write_count++, &cpn, badblkp);
      if (status)
        return status;
      curr_loc = control_data_start;
    }

    // If end of running bad blocks map reached, mark end.
    if (i == ndm->num_rbb) {
      // Mark end of map before writing whole map to flash.
      WR32_LE((ui32)-1, &ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);
      WR32_LE((ui32)-1, &ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);
      break;
    }

    // Add next running bad block pair.
    WR32_LE(ndm->run_bad_blk[i].key, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);
    WR32_LE(ndm->run_bad_blk[i].val, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

#if NDM_DEBUG
    printf("  -> run_bad_blk[%2u]: key = %u, val = %u\n", i, ndm->run_bad_blk[i].key,
           ndm->run_bad_blk[i].val);
#endif
  }

  // Write the NDM partitions if any, one at a time.
  for (i = 0; i < ndm->num_partitions; ++i) {
#if NDM_PART_USER
    ui32 j;
#endif

    size_t partition_size = sizeof(NDMPartition);
    if (ndm->version_2) {
      PfAssert(ndm->num_partitions == 1);
      NDMPartitionInfo* info = (NDMPartitionInfo*)(ndm->partitions);
      partition_size += sizeof(ui32) + info->user_data.data_size;
    }

    // If next write would go past end of control page, write page.
    if (curr_loc + partition_size > ndm->page_size) {
      status = wr_ctrl_page(ndm, write_count++, &cpn, badblkp);
      if (status)
        return status;
      curr_loc = control_data_start;
    }

    // Write partition first block.
    WR32_LE(ndm->partitions[i].first_block, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

    // Write partition number of blocks.
    WR32_LE(ndm->partitions[i].num_blocks, &ndm->main_buf[curr_loc]);
    curr_loc += sizeof(ui32);

#if NDM_PART_USER
    // Write the user defined ui32s.
    for (j = 0; j < NDM_PART_USER; ++j) {
      WR32_LE(ndm->partitions[i].user[j], &ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);
    }
#endif

    // Write the partition name.
    strncpy((char*)&ndm->main_buf[curr_loc], ndm->partitions[i].name, NDM_PART_NAME_LEN);
    curr_loc += NDM_PART_NAME_LEN;

    // Write the partition type.
    ndm->main_buf[curr_loc++] = ndm->partitions[i].type;

    if (ndm->version_2) {
      PfAssert(ndm->num_partitions == 1);
      NDMPartitionInfo* info = (NDMPartitionInfo*)(ndm->partitions);
      WR32_LE(info->user_data.data_size, &ndm->main_buf[curr_loc]);
      curr_loc += sizeof(ui32);
      memcpy(&ndm->main_buf[curr_loc], info->user_data.data, info->user_data.data_size);
      curr_loc += info->user_data.data_size;
    }

#if NDM_DEBUG
    printf("  -> partition[%2u]:\n", i);
    printf("    - name        = %s\n", ndm->partitions[i].name);
    printf("    - first block = %u\n", ndm->partitions[i].first_block);
    printf("    - num blocks  = %u\n", ndm->partitions[i].num_blocks);
#if NDM_PART_USER
    for (j = 0; j < NDM_PART_USER; ++j)
      printf("    - user[%u]     = %u\n", j, ndm->partitions[i].user[j]);
#endif
#endif  // NDM_DEBUG
  }

  // Write last control page and return status.
  return wr_ctrl_page(ndm, write_count, &cpn, badblkp);
}

// mark_ctrl_bblock: Record a control block failure and get a new one
//
//      Inputs: ndm = pointer to NDM control block
//              *cblkp = control block that failed
//      Output: *cblkp = new control block
//
//     Returns: 0 on success, -1 on failure
//
static int mark_ctrl_bblock(NDM ndm, ui32* cblkp) {
  ui32 bad_blk = *cblkp;

  // Clear virtual to physical translation caches.
  ndm->last_wr_vbn = ndm->last_rd_vbn = (ui32)-1;

  // Adjust bad block count. If too many, error.
  if (++ndm->num_bad_blks > ndm->max_bad_blks)
    return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

  // Update the running bad block map with this block.
  ndm->run_bad_blk[ndm->num_rbb].key = bad_blk;
  ndm->run_bad_blk[ndm->num_rbb].val = (ui32)-1;
  ++ndm->num_rbb;

  // Get a new free block for the other control block.
  for (; ndm->free_ctrl_blk != (ui32)-1; --ndm->free_ctrl_blk) {
    // If we've ran out of free blocks, error.
    if (ndm->free_ctrl_blk < ndm->free_virt_blk) {
      ndm->free_ctrl_blk = ndm->free_virt_blk = (ui32)-1;
      break;
    }

    // If initial bad block, skip it.
    if (ndmInitBadBlock(ndm, ndm->free_ctrl_blk))
      continue;

    // We found a free block. Remember it.
    *cblkp = ndm->free_ctrl_blk;

    // Update the right control block pointer with this free block.
    if (bad_blk == ndm->ctrl_blk0)
      ndm->ctrl_blk0 = ndm->free_ctrl_blk;
    else if (bad_blk == ndm->ctrl_blk1)
      ndm->ctrl_blk1 = ndm->free_ctrl_blk;
    else
      return FsError2(NDM_ASSERT, EINVAL);

    // Update pointer to next free control block and return success.
    --ndm->free_ctrl_blk;
    return 0;
  }

  // If no free block found for control block, error.
  return FsError2(NDM_NO_FREE_BLK, ENOSPC);
}

// get_free_virt_blk: Get next free block reserved for replacing bad
//              virtual blocks (starts at lowest and goes up)
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: Free block if successful, (ui32)-1 if none are free
//
static ui32 get_free_virt_blk(NDM ndm) {
  ui32 free_b = ndm->free_virt_blk;

  // Check if there are any free blocks left.
  if (free_b != (ui32)-1) {
    ui32 b = free_b + 1;

    // Skip past initial bad blocks.
    while (b <= ndm->free_ctrl_blk && ndmInitBadBlock(ndm, b))
      ++b;

    // If we haven't gone into the blocks reserved for swapping bad
    // control blocks, update the free block pointer.
    if (b <= ndm->free_ctrl_blk)
      ndm->free_virt_blk = b;
    else
      ndm->free_virt_blk = ndm->free_ctrl_blk = (ui32)-1;
  }

  // Return free block number.
  return free_b;
}

// mark_extra_bblock: Mark a block bad while in middle of a transfer
//              of another bad block (this is the transfer to block)
//
//      Inputs: ndm = pointer to NDM control block
//   In/Output: *bnp = bad block IN, substitute free block OUT
//
//     Returns: 0 on success, -1 on failure
//
static int mark_extra_bblock(NDM ndm, ui32* bnp) {
  ui32 free_b, i;
  int found;

  // Clear virtual to physical translation caches.
  ndm->last_wr_vbn = ndm->last_rd_vbn = (ui32)-1;

  // Adjust number of bad blocks. If too many, error.
  if (++ndm->num_bad_blks > ndm->max_bad_blks)
    return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

  // Get a free block to replace the bad block.
  free_b = get_free_virt_blk(ndm);
  if (free_b == (ui32)-1)
    return FsError2(NDM_NO_FREE_BLK, ENOSPC);

  // Check that bad block is in the running bad block map only as a
  // transfer to block (.val field) and only once.
  for (i = 0, found = FALSE;; ++i) {
    // If end of map, either found exactly once or error.
    if (i == ndm->num_rbb) {
      if (found)
        break;
      return FsError2(NDM_ASSERT, EINVAL);
    }

    // Bad block cannot be in the map as a transfer from block (.key).
    if (ndm->run_bad_blk[i].key == *bnp)
      return FsError2(NDM_ASSERT, EINVAL);

    // If bad block in map, ensure first occurrence and remember find.
    if (ndm->run_bad_blk[i].val == *bnp) {
      if (found)
        return FsError2(NDM_ASSERT, EINVAL);
      found = TRUE;
    }
  }

  // Add new entry to running bad block map with bad block.
  ndm->run_bad_blk[ndm->num_rbb].key = *bnp;
  ndm->run_bad_blk[ndm->num_rbb].val = free_b;
  ++ndm->num_rbb;

  // Output the selected free block number and return success.
  *bnp = free_b;
  return 0;
}

// run_bad_block: Check if a block is a running bad block
//
//      Inputs: ndm = pointer to NDM control block
//              b = block to check
//
//     Returns: TRUE if block is bad, FALSE otherwise
//
static int run_bad_block(CNDM ndm, ui32 b) {
  ui32 i;

  // Walk the run bad blocks map to see if block is in there.
  for (i = 0; i < ndm->num_rbb; ++i)
    if (ndm->run_bad_blk[i].key == b)
      return TRUE;

  // Block number not found in list. Return FALSE.
  return FALSE;
}

//     get_pbn: Get a physical block number from a virtual one
//
//      Inputs: ndm = pointer to NDM control block
//              vbn = virtual block number
//              reason = reason for lookup: RD_MAPPING or WR_MAPPING
//
//     Returns: physical block number on success, else (ui32)-1
//
static ui32 get_pbn(NDM ndm, ui32 vbn, int reason) {
  ui32 bn, i;
#if NDM_DEBUG
  ui32 j;

  // Ensure the initial bad blocks map is valid (no duplicates).
  for (i = 0;; ++i) {
    if (i > ndm->max_bad_blks)
      return (ui32)-1;
    if (ndm->init_bad_blk[i] == ndm->num_dev_blks)
      break;
    for (j = i + 1; j <= ndm->max_bad_blks; ++j) {
      if (ndm->init_bad_blk[j] == ndm->num_dev_blks)
        break;
      if (ndm->init_bad_blk[i] == ndm->init_bad_blk[j])
        return (ui32)-1;
    }
  }

  // Ensure running bad blocks map is valid (no key/value duplicates).
  for (i = 0; i < ndm->num_rbb; ++i) {
    if (ndm->run_bad_blk[i].key == (ui32)-1)
      return (ui32)-1;
    for (j = i + 1; j < ndm->num_rbb; ++j) {
      if (ndm->run_bad_blk[i].key == ndm->run_bad_blk[j].key)
        return (ui32)-1;
      if (ndm->run_bad_blk[i].val == ndm->run_bad_blk[j].val && ndm->run_bad_blk[i].val != (ui32)-1)
        return (ui32)-1;
    }
  }
#endif  // NDM_DEBUG

  // If no bad blocks, physical block number matches virtual.
  if (ndm->num_bad_blks == 0)
    return vbn;

  // If virtial block number matches last read or write lookup, return
  // last read or write physical block number.
  if (vbn == ndm->last_wr_vbn)
    return ndm->last_wr_pbn;
  if (vbn == ndm->last_rd_vbn)
    return ndm->last_rd_pbn;

  // First determine where the block was before any running bad blocks
  // happened. Do this by walking the initial bad blocks map.
  for (i = 0, bn = vbn;; ++i) {
    // Should never pass last entry, which holds 'num_dev_blks'.
    if (i > ndm->max_bad_blks)
      return (ui32)FsError2(NDM_ASSERT, EINVAL);

    // 'i' is the number of initial bad blocks preceding the indexed`
    // initial bad block. Break when the number of volume blocks and
    // skipped bad blocks is less than the indexed initial bad block.
    if (vbn + i < ndm->init_bad_blk[i]) {
      // Add 'i' to account for the initial bad blocks that precede
      // this block. This mapping of the initial bad blocks supports
      // use of images programmed using the 'skip bad block' method.
      bn += i;
      break;
    }
  }

  // At this point the initial bad block cannot be in the NDM area.
  if (bn >= ndm->frst_reserved)
    return (ui32)FsError2(NDM_ASSERT, EINVAL);

  // Walk the running bad block map replacing initial block with the
  // most current version (if any).
  for (i = 0; i < ndm->num_rbb; ++i)
    if (ndm->run_bad_blk[i].key == bn)
      bn = ndm->run_bad_blk[i].val;

  // Ensure the block number is valid.
  if (bn >= ndm->num_dev_blks)
    return (ui32)FsError2(NDM_ASSERT, EINVAL);

  // Update last read or write lookup cache.
  if (reason == WR_MAPPING) {
    ndm->last_wr_pbn = bn;
    ndm->last_wr_vbn = vbn;
  } else {  // reason == RD_MAPPING
    ndm->last_rd_pbn = bn;
    ndm->last_rd_vbn = vbn;
  }

  // Return physical block number.
  return bn;
}

//  write_page: Write a page to flash for FTL
//
//      Inputs: ndm = pointer to NDM control block
//              vpn = virtual page number
//              buf = pointer to main page buffer array
//              spare = pointer to spare page buffer array
//              action = NDM_ECC or NDM_ECC_VAL
//
//     Returns: 0 on success, -2 on fatal error
//
static int write_page(NDM ndm, uint32_t vpn, const ui8* buf, ui8* spare, int action) {
  uint32_t vbn, bn, pn;
  int rc;

  // Compute the virtual block number and check for error.
  vbn = vpn / ndm->pgs_per_blk;
  if (vbn >= ndm->num_vblks) {
    FsError2(NDM_ASSERT, EINVAL);
    return -2;
  }

  // Write page until successful or failure other than bad block.
  for (;;) {
    // Get the physical block number from virtual one.
    bn = get_pbn(ndm, vbn, WR_MAPPING);
    if (bn == (ui32)-1)
      return -2;

    // Compute physical page number.
    pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

    // Call TargetNDM driver to write the page.
    rc = ndm->write_page(pn, buf, spare, action, ndm->dev);

    // Return 0 if successful.
    if (rc == 0)
      return 0;

    // Else if chip error, mark block bad and do bad block recovery.
    else if (rc == -1) {
      if (ndmMarkBadBlock(ndm, pn, WRITE_PAGE))
        return -2;
    }

    // Else fatal error, return -2.
    else {
      PfAssert(rc == -2);
      FsError2(NDM_EIO, EIO);
      return -2;
    }
  }
}  // lint !e818

//   read_page:  FTL driver function - read page (data only)
//
//      Inputs: vpn = virtual page number
//              buf = pointer to buffer to copy data to
//              ndm = NDM control block handle
//
//     Returns: 0 on success, -1 on uncorrectable ECC error, -2 on
//              permanent fatal error, 1 if block page belongs to
//              needs to be recycled
//
static int read_page(ui32 vpn, void* buf, NDM ndm) {
  ui32 vbn, bn, pn;
  int status;

  // Compute the virtual block number based on virtual page number.
  vbn = vpn / ndm->pgs_per_blk;
  if (vbn >= ndm->num_vblks) {
    FsError2(NDM_ASSERT, EINVAL);
    return -2;
  }

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // Get the physical block number from virtual one.
  bn = get_pbn(ndm, vbn, RD_MAPPING);
  if (bn == (ui32)-1) {
    semPostBin(ndm->sem);
    return -2;
  }

  // Compute physical page number.
  pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

  // Read decode page data.
  status = ndm->read_page(pn, buf, ndm->spare_buf, ndm->dev);

  // Release exclusive access to TargetNDM internals.
  semPostBin(ndm->sem);

  // If error, set errno. Return status.
  if (status) {
    if (status == -1)
      FsError2(NDM_RD_ECC_FAIL, EINVAL);
    else if (status == -2)
      FsError2(NDM_EIO, EIO);
  }
  return status;
}

// Global Function Definitions

//   ndmWritePage: FTL driver function - write page (data + spare)
//
//      Inputs: vpn = virtual page number
//              data = pointer to buffer containing data to write
//              spare = pointer to buffer containing spare bytes
//              ndm = NDM control block handle
//
//     Returns: 0 on success, -2 on fatal error
//
int ndmWritePage(uint32_t vpn, const ui8* data, ui8* spare, NDM ndm) {
  int action, status;

  // If volume block, just ECC, else request validity checks too.
  if (RD32_LE(&spare[5]) == (ui32)-1)
    action = NDM_ECC;
  else
    action = NDM_ECC_VAL;

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // Write page to flash for FTL.
  status = write_page(ndm, vpn, data, spare, action);

  // Release exclusive access to NDM and return status.
  semPostBin(ndm->sem);
  return status;
}  // lint !e818

// ndmReadSpare: FTL driver function - read/decode page spare area
//
//      Inputs: vpn = virtual page number
//              spare = buffer to read sparea area into
//              ndm = NDM control block handle
//
//     Returns: 0 on success, -1 on ECC error, -2 on fatal error, 1 if
//              block page belongs to needs to be recycled
//
int ndmReadSpare(uint32_t vpn, ui8* spare, NDM ndm) {
  uint32_t vbn, bn, pn;
  int status;

  // Compute virtual block number. Return -2 if out-of-range.
  vbn = vpn / ndm->pgs_per_blk;
  if (vbn >= ndm->num_vblks) {
    FsError2(NDM_ASSERT, EINVAL);
    return -2;
  }

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // Get physical block number from virtual one. Return -2 if error.
  bn = get_pbn(ndm, vbn, RD_MAPPING);
  if (bn == (ui32)-1) {
    semPostBin(ndm->sem);
    return -2;
  }

  // Compute physical page number.
  pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

  // Read and decode spare. Call FsError2() if error.
  status = ndm->read_decode_spare(pn, spare, ndm->dev);
  if (status < 0)
    FsError2(NDM_EIO, EIO);

  // Release exclusive access to NDM and return status.
  semPostBin(ndm->sem);
  return status;
}

// ndmCheckPage: FTL driver function - determine status of a page
//
//      Inputs: vpn = virtual page number
//              data = buffer that will hold page data
//              spare = buffer that will hold page spare
//              ndm = NDM control block handle
//
//     Returns: -1 if error, else NDM_PAGE_ERASED (0), NDM_PAGE_VALID
//              (1), or NDM_PAGE_INVALID (2)
//
int ndmCheckPage(uint32_t vpn, ui8* data, ui8* spare, NDM ndm) {
  uint32_t vbn, bn, pn;
  int status;

  // Compute the virtual block number.
  vbn = vpn / ndm->pgs_per_blk;
  if (vbn >= ndm->num_vblks)
    return FsError2(NDM_ASSERT, EINVAL);

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // Get the physical block number from virtual block number.
  bn = get_pbn(ndm, vbn, RD_MAPPING);
  if (bn == (ui32)-1) {
    semPostBin(ndm->sem);
    return -1;
  }

  // Compute physical page number.
  pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

  // Call the NDM driver to determine page status.
  if (ndm->check_page(pn, data, spare, &status, ndm->dev)) {
    semPostBin(ndm->sem);
    return FsError2(NDM_EIO, EIO);
  }

  // Release exclusive access to NDM and return status.
  semPostBin(ndm->sem);
  return status;
}

// ndmTransferPage: FTL driver function - transfer a page
//
//      Inputs: old_vpn = old virtual page number
//              new_vpn = new virtual page number
//              buf = temporary buffer for swapping main page data
//              spare = buffer holding new page's spare data
//              ndm = NDM control block handle
//
//     Returns: 0 on success, -2 on fatal error, 1 on ECC decode error
//
int ndmTransferPage(uint32_t old_vpn, uint32_t new_vpn, ui8* buf, ui8* spare, NDM ndm) {
  uint32_t old_vbn, new_vbn, old_bn, new_bn, old_pn, new_pn;
  int status, action;

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // If FTL valid block counts, do ECC + validity, else ECC only.
  if (RD32_LE(&spare[5]) != (ui32)-1)
    action = NDM_ECC_VAL;
  else
    action = NDM_ECC;

  // Compute the old/new virtual block numbers based on virtual pages.
  old_vbn = old_vpn / ndm->pgs_per_blk;
  new_vbn = new_vpn / ndm->pgs_per_blk;
  if (old_vbn >= ndm->num_vblks || new_vbn >= ndm->num_vblks) {
    FsError2(NDM_ASSERT, EINVAL);
    semPostBin(ndm->sem);
    return -2;
  }

  // Get the physical old block number from virtual one.
  old_bn = get_pbn(ndm, old_vbn, RD_MAPPING);
  if (old_bn == (ui32)-1) {
    semPostBin(ndm->sem);
    return -2;
  }

  // Compute the old physical page number.
  old_pn = old_bn * ndm->pgs_per_blk + old_vpn % ndm->pgs_per_blk;

  // Copy page to new block until success or error other than bad block
  for (;;) {
    // Get the physical new block number for virtual one.
    new_bn = get_pbn(ndm, new_vbn, WR_MAPPING);
    if (new_bn == (ui32)-1) {
      semPostBin(ndm->sem);
      return -2;
    }

    // Compute the new physical page number.
    new_pn = new_bn * ndm->pgs_per_blk + new_vpn % ndm->pgs_per_blk;

    // Transfer data from old page to new page.
    status = ndm->xfr_page(old_pn, new_pn, buf, ndm->tmp_spare, spare, action, ndm->dev_ndm);

    // Break if success (0) or ECC decode error (1).
    if (status >= 0)
      break;

    // Else if fatal error (-2), break to return -2.
    if (status == -2) {
      FsError2(NDM_EIO, EIO);
      break;
    }

    // Else chip error (-1), mark block bad and do bad block recovery.
    else {
      PfAssert(status == -1);
      if (ndmMarkBadBlock(ndm, new_pn, WRITE_PAGE)) {
        status = -2;
        break;
      }
    }
  }

  // Release exclusive access to NDM and return status.
  semPostBin(ndm->sem);
  return status;
}  // lint !e818

#if INC_FTL_NDM_MLC
// ndmPairOffset: FTL driver function (MLC NAND) - pair offset
//
//      Inputs: page_offset = page offset within block
//              ndm = NDM control block handle
//
//     Returns: Pair page offset within block if any, page offset
//              otherwise
//
uint32_t ndmPairOffset(uint32_t page_offset, CNDM ndm) {
  return ndm->pair_offset(page_offset, ndm->dev);
}
#endif  // INC_FTL_NDM_MLC

// ndmMarkBadBlock: Mark virtual block bad and do bad block recovery
//
//      Inputs: ndm = pointer to NDM control block
//              arg = bad block or page depending on cause
//              cause = ERASE_BLOCK or WRITE_PAGE failure
//
//     Returns: 0 on success, -1 on failure
//
int ndmMarkBadBlock(NDM ndm, ui32 arg, ui32 cause) {
  ui32 bad_b, bad_pn, free_b, i, old_pn, new_pn;
  int status, transfer_finished;

  // Clear virtual to physical translation caches.
  ndm->last_wr_vbn = ndm->last_rd_vbn = (ui32)-1;

  // Get a free block to replace the bad virtual block.
  free_b = get_free_virt_blk(ndm);
  if (free_b == (ui32)-1)
    return FsError2(NDM_NO_FREE_BLK, ENOSPC);

  // Based on cause, figure out bad block and first bad page.
  if (cause == ERASE_BLOCK) {
    bad_b = arg;
    bad_pn = bad_b * ndm->pgs_per_blk;
  } else {
    bad_pn = arg;
    bad_b = bad_pn / ndm->pgs_per_blk;
  }

  // Search the running bad block list for this block.
  for (i = 0;; ++i) {
    // Check if reached end of list without finding match.
    if (i == ndm->num_rbb) {
      // Adjust bad block count. If too many, return -1.
      if (++ndm->num_bad_blks > ndm->max_bad_blks)
        return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

      // Add block as last list entry and then break.
      ndm->run_bad_blk[ndm->num_rbb].key = bad_b;
      ndm->run_bad_blk[ndm->num_rbb].val = free_b;
      ++ndm->num_rbb;
      break;
    }

    // If in list (due to recovery of bad block transfer), update it.
    if (ndm->run_bad_blk[i].key == bad_b) {
      ndm->run_bad_blk[i].val = free_b;
      break;
    }
  }

  // Loop until bad block recovery is finished.
  for (transfer_finished = FALSE;;) {
    // Call driver to erase the free block.
    status = ndm->erase_block(free_b * ndm->pgs_per_blk, ndm->dev);

    // Check if the block erase succeeded.
    if (status == 0) {
      // Finished if block copy unneeded.
      if (cause == ERASE_BLOCK)
        break;

      // Prepare control information with bad block transfer data.
      ndm->xfr_tblk = free_b;
      ndm->xfr_fblk = bad_b;
      ndm->xfr_bad_po = bad_pn % ndm->pgs_per_blk;

      // Write TargetNDM metadata (includes bad block lists).
      if (ndmWrCtrl(ndm))
        return -1;

      // Transfer data from the bad block to the free block.
      old_pn = bad_b * ndm->pgs_per_blk;
      new_pn = free_b * ndm->pgs_per_blk;
      for (i = 0;; ++i, ++old_pn, ++new_pn) {
        int action;

        // If bad write page reached, transfer finished.
        if (i == ndm->xfr_bad_po) {
          transfer_finished = TRUE;
          break;
        }

        // If end of block reached, transfer finished.
        if (i >= ndm->pgs_per_blk) {
          transfer_finished = TRUE;
          break;
        }

        // Call driver to see if main and spare areas are erased.
        status = ndm->page_blank(old_pn, ndm->main_buf, ndm->spare_buf, ndm->dev);

        // If erased, skip page. Else if I/O error, return -1.
        if (status == TRUE)
          continue;
        else if (status < 0)
          return FsError2(NDM_EIO, EIO);

        // Read main data. Return -1 if ECC or fatal error.
        status = ndm->read_page(old_pn, ndm->main_buf, ndm->spare_buf, ndm->dev);
        if (status < 0)
          return FsError2(NDM_EIO, EIO);

        // Read old spare data. Return -1 if ECC or fatal error.
        status = ndm->read_decode_spare(old_pn, ndm->spare_buf, ndm->dev);
        if (status < 0)
          return FsError2(NDM_EIO, EIO);

        // If FTL volume page, just ECC the spare bytes.
        if (RD32_LE(&ndm->spare_buf[5]) == (ui32)-1)
          action = NDM_ECC;

        // Else map page, ECC the spare bytes and prep validity check.
        else
          action = NDM_ECC_VAL;

        // Write page to new location. Break if error occurs.
        status = ndm->write_page(new_pn, ndm->main_buf, ndm->spare_buf, action, ndm->dev);
        if (status)
          break;
      }
    }

    // Break if the block transfer is finished.
    if (transfer_finished)
      break;

    // Return -1 if fatal error.
    if (status == -2)
      return FsError2(NDM_EIO, EIO);

    // Else bad block. Mark it. Return -1 if error.
    else {
      PfAssert(status == -1);
      if (mark_extra_bblock(ndm, &free_b))
        return -1;
    }
  }

  // Update control information to clear the bad block transfer state.
  ndm->xfr_tblk = (ui32)-1;
  if (ndmWrCtrl(ndm))
    return -1;

  // Return success.
  return 0;
}

//   ndmWrCtrl: Write NDM control information to flash
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on error
//
int ndmWrCtrl(NDM ndm) {
  ui32 ctrl_blk, first_page = ndm->next_ctrl_start;
  int status;

  // Keep writing control information until successful.
  for (status = 0;;) {
    // Check if this is first write to this control block.
    if (first_page % ndm->pgs_per_blk == 0) {
      // Call driver to erase the block. Return -1 if fatal error.
      status = ndm->erase_block(first_page, ndm->dev);
      if (status == -2)
        return FsError2(NDM_EIO, EIO);
      if (status)
        ctrl_blk = first_page / ndm->pgs_per_blk;
    }

    // Check if block erase command succeeded or was unneeded.
    if (status == 0) {
      // Write the control information. Return -1 if fatal error.
      // Outputs number of bad block, if there is a failure.
      status = wr_ctrl_info(ndm, first_page, &ctrl_blk);
      if (status == -2)
        return -1;

      // Else break if successful.
      else if (status == 0)
        break;
      PfAssert(status == -1);
    }

    // Block failed. Mark it bad and get new control block.
    if (mark_ctrl_bblock(ndm, &ctrl_blk))
      return -1;
    first_page = ctrl_blk * ndm->pgs_per_blk;
  }

  // For SLC devices, start next control write immediately after the
  // last page in this control information.
  first_page = ndm->last_ctrl_page + 1;

#if INC_FTL_NDM_MLC
  // For MLC devices, take into account page pair offset so that new
  // write can not affect old metadata in case of power off.
  first_page = ndmPastPrevPair(ndm, first_page);
#endif

  // If start of next write falls outside the current control block,
  // move to the other control block.
  ctrl_blk = ndm->last_ctrl_page / ndm->pgs_per_blk;
  if (first_page / ndm->pgs_per_blk != ctrl_blk) {
    if (ctrl_blk == ndm->ctrl_blk0)
      ctrl_blk = ndm->ctrl_blk1;
    else if (ctrl_blk == ndm->ctrl_blk1)
      ctrl_blk = ndm->ctrl_blk0;
    else
      return FsError2(NDM_ASSERT, EINVAL);
    first_page = ctrl_blk * ndm->pgs_per_blk;
  }

#if RDBACK_CHECK
  // Read-back verify the TargetNDM metadata.
  ndmCkMeta(ndm);
#endif

  // Save start of next control info write and return success.
  ndm->next_ctrl_start = first_page;
  return 0;
}

// ndmUnformat: Unformat (erase all good blocks) an NDM
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
int ndmUnformat(NDM ndm) {
  int status;
  ui32 b;

  // Grab exclusive access to the NDM.
  semPend(ndm->sem, WAIT_FOREVER);

#if NV_NDM_CTRL_STORE
  // Invalidate the saved first page of NDM control information.
  NvNdmCtrlPgWr(0);
#endif

  // Walk through all the blocks, erasing good ones.
  for (b = 0; b < ndm->num_dev_blks; ++b) {
    // If block is an initial bad block, skip it.
    if (ndmInitBadBlock(ndm, b))
      continue;

    // If block is a running bad block, skip it.
    if (run_bad_block(ndm, b))
      continue;

    // Erase block. Ignore return code.
    ndm->erase_block(b * ndm->pgs_per_blk, ndm->dev);
  }

  // Remove all the volumes on the device.
  status = ndmDelVols(ndm);

  // Release exclusive access to the NDM and return status.
  semPostBin(ndm->sem);
  return status;
}  // lint !e818

// ndmGetNumVBlocks: Return number of virtual blocks in NDM
//
//       Input: ndm = pointer to NDM control block
//
ui32 ndmGetNumVBlocks(CNDM ndm) { return ndm->num_vblks; }

// ndmAddVolFTL: Add FTL volume to NDM partition
//
//      Inputs: ndm = pointer to NDM control block
//              part_num = NDM partition number
//              ftl_cfg = pointer to FTL config structure
//              xfs = XFS volume information
//
//     Returns: FTL handle on success, NULL on error
//
void* ndmAddVolFTL(NDM ndm, ui32 part_num, FtlNdmVol* ftl_cfg, XfsVol* xfs) {
  NDMPartition* part;

  // Check partition number.
  if (part_num >= ndm->num_partitions) {
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }
  part = &ndm->partitions[part_num];

  // Check partition first block and number of blocks.
  if (part->first_block + part->num_blocks > ndm->num_vblks) {
    FsError2(NDM_CFG_ERR, ENOSPC);
    return NULL;
  }

  // Assign the NDM-supplied configuration.
  ftl_cfg->page_size = ndm->page_size;
  ftl_cfg->eb_size = ndm->eb_size;
  ftl_cfg->block_size = ndm->block_size;
  ftl_cfg->ndm = ndm;
  ftl_cfg->start_page = part->first_block * ndm->pgs_per_blk;
  ftl_cfg->num_blocks = part->num_blocks;
  xfs->name = part->name;

  // Add an FTL to this partition. Return status.
  return FtlnAddVol(ftl_cfg, xfs);
}

// ndmReadPages: FTL driver function - read multiple consecutive
//              pages from a single block (data only)
//
//      Inputs: vpn = starting virtual page number
//              count = number of consecutive virtual pages to read
//              buf = pointer to buffer to copy main page data to
//              spare = points to array of page spare data sets
//              ndm = NDM control block handle
//
//     Returns: -2 on fatal error, -1 on error, 0 on success, 1 if
//              block needs to be recycled
//
int ndmReadPages(uint32_t vpn, uint32_t count, ui8* buf, ui8* spare, NDM ndm) {
  int status;

  // If NDM driver supplies read_pages(), use it.
  if (ndm->read_pages) {
    uint32_t vbn, bn, pn;

    // Compute the virtual block number based on virtual page number.
    vbn = vpn / ndm->pgs_per_blk;
    if (vbn >= ndm->num_vblks) {
      FsError2(NDM_ASSERT, EINVAL);
      return -2;
    }

    // Grab exclusive access to TargetNDM internals.
    semPend(ndm->sem, WAIT_FOREVER);

    // Get the physical block number from virtual one.
    bn = get_pbn(ndm, vbn, RD_MAPPING);
    if (bn == (ui32)-1) {
      semPostBin(ndm->sem);
      return -2;
    }

    // Compute starting physical page number.
    pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

    // Read pages.
    status = ndm->read_pages(pn, count, buf, spare, ndm->dev);

    // Release exclusive access to NDM and return status.
    semPostBin(ndm->sem);
    return status;
  }

  // Else loop over all pages, reading one at a time.
  else {
    uint32_t i;
    int rd_status;

    // Loop over virtual pages.
    for (status = 0, i = 0; i < count; ++i, ++vpn, buf += ndm->page_size) {
      // Issue current page read request.
      rd_status = read_page(vpn, buf, ndm);

      // If error, return.
      if (rd_status < 0)
        return rd_status;

      // If recycles needed for block, set return status.
      if (rd_status == 1)
        status = 1;
    }

    // Return status.
    return status;
  }
}

// ndmWritePages: FTL driver function - write multiple consecutive
//              pages to a single block (data only)
//
//      Inputs: vpn = starting virtual page number
//              count = number of consecutive virtual pages to write
//              data = points to array of page main data sets
//              spare = points to array of page spare data sets
//              ndm = NDM control block handle
//
//     Returns: -1 on error, 0 on success
//
int ndmWritePages(uint32_t vpn, uint32_t count, const ui8* data, ui8* spare, NDM ndm) {
  int action, rc = 0;

  // Ensure all writes are to the same virtual block.
  PfAssert(count);
  PfAssert(vpn / ndm->pgs_per_blk == (vpn + count - 1) / ndm->pgs_per_blk);

  // Else for FTL, prepare ECC and, if map page, validity checks too.
  if (RD32_LE(&((ui8*)spare)[5]) == (ui32)-1)
    action = NDM_ECC;
  else
    action = NDM_ECC_VAL;

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // If NDM driver supplies write_pages(), use it.
  if (ndm->write_pages) {
    uint32_t vbn;

    // Compute the virtual block number based on virtual page number.
    vbn = vpn / ndm->pgs_per_blk;
    if (vbn >= ndm->num_vblks) {
      semPostBin(ndm->sem);
      return FsError2(NDM_ASSERT, EINVAL);
    }

    // Writing to flash until success or failure other than bad block.
    for (;;) {
      uint32_t bn, pn;

      // Get the physical block number from virtual one.
      bn = get_pbn(ndm, vbn, WR_MAPPING);
      if (bn == (uint32_t)-1) {
        rc = -1;
        break;
      }

      // Compute starting physical page number.
      pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

      // Write pages. If successful, break from loop.
      rc = ndm->write_pages(pn, count, data, spare, action, ndm->dev);
      if (rc == 0)
        break;

      // If fatal error, break to return -1.
      if (rc == -2) {
        rc = FsError2(NDM_EIO, EIO);
        break;
      }

      // Else bad block, mark it bad. If error, break to return -1.
      else {
        PfAssert(rc == -1);
        if (ndmMarkBadBlock(ndm, pn, WRITE_PAGE))
          break;
      }
    }
  }

  // Else loop over all pages, writing one at a time.
  else {
    uint32_t past = vpn + count;
    const ui8* curr_data = data;
    ui8* curr_spare = spare;

    // Loop over virtual pages.
    for (; vpn < past; ++vpn) {
      // Write current page.
      rc = write_page(ndm, vpn, curr_data, curr_spare, action);
      if (rc)
        break;

      // Advance data pointer and spare pointer.
      curr_data += ndm->page_size;
      curr_spare += ndm->eb_size;
    }
  }

  // Release exclusive access to NDM and return status.
  semPostBin(ndm->sem);
  return rc;
}

// ndmGetNumPartitions: Retrieve number of current partitions in table
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: Current number of partitions in NDM
//
ui32 ndmGetNumPartitions(CNDM ndm) { return ndm->num_partitions; }

// ndmSetNumPartitions: Set number of current partitions in table
//
//      Inputs: ndm = pointer to NDM control block
//              num_partitions = number of desired partitions
//
//     Returns: 0 on success, -1 on failure
//
int ndmSetNumPartitions(NDM ndm, ui32 num_partitions) {
  NDMPartition* new_partitions;

  // If the number of partitions is unchanged, simply return success.
  if (num_partitions == ndm->num_partitions)
    return 0;

  // If number of partitions is 0, this is a delete table call.
  if (num_partitions == 0) {
    ndmDeletePartitionTable(ndm);
    return 0;
  }

  // Allocate space for the new partitions table.
  new_partitions = FsCalloc(num_partitions, sizeof(NDMPartition));
  if (new_partitions == NULL)
    return FsError2(NDM_ENOMEM, ENOMEM);

  // If there is a current partitions table, copy as much of it as
  // possible to the new table before removing it.
  if (ndm->partitions) {
    ui32 i, max_i = MIN(num_partitions, ndm->num_partitions);

    for (i = 0; i < max_i; ++i)
      memcpy(&new_partitions[i], &ndm->partitions[i], sizeof(NDMPartition));
    FsFree(ndm->partitions);
  }

  // Set the new partition information in the NDM.
  ndm->partitions = new_partitions;
  ndm->num_partitions = num_partitions;

  // Return success.
  return 0;
}

// ndmGetPartitionInfo: Reads partition information.
//
//      Inputs: ndm = pointer to NDM control block.
//
//     Returns: Pointer to the partition data, if available.
//
//     Preconditions: Writing of header version 2 is enabled.
//
const NDMPartitionInfo* ndmGetPartitionInfo(CNDM ndm) {
  if (!ndm->version_2) {
    return NULL;
  }

  return (NDMPartitionInfo*)ndm->partitions;
}

// ndmWritePartitionInfo: Writes a partition entry into partitions table.
//
//      Inputs: ndm = pointer to NDM control block
//              partition = buffer to get partition information from
//
//     Returns: 0 on success, -1 on error
//
//     Preconditions: Writing of header version 2 is enabled.
//
int ndmWritePartitionInfo(NDM ndm, const NDMPartitionInfo* partition) {
  PfAssert(partition->user_data.data_size % sizeof(ui32) == 0);

  if (ndm->num_partitions > 1) {
    return FsError2(NDM_CFG_ERR, EINVAL);
  }

  // Check partition boundaries.
  if (partition->basic_data.first_block >= ndm->num_vblks ||
      partition->basic_data.first_block + partition->basic_data.num_blocks > ndm->num_vblks) {
    return FsError2(NDM_CFG_ERR, EINVAL);
  }

  size_t partition_size = sizeof(NDMPartition) + sizeof(ui32) + partition->user_data.data_size;

  // Allocate space for the new partition info.
  NDMPartitionInfo* new_partition = FsCalloc(1, partition_size);
  if (!new_partition) {
    return FsError2(NDM_ENOMEM, ENOMEM);
  }

  if (ndm->partitions) {
    FsFree(ndm->partitions);
  }

  // Set the new partition information in the NDM.
  ndm->partitions = &new_partition->basic_data;
  ndm->num_partitions = 1;
  ndm->version_2 = TRUE;

  // Copy the partition info.
  memcpy(new_partition, partition, partition_size);
  return 0;
}

// ndmGetPartition: Return partition entry handle
//
//      Inputs: ndm = pointer to NDM control block
//              part_num = partition number
//
//     Returns: partition handle on success, NULL on error
//
const NDMPartition* ndmGetPartition(CNDM ndm, ui32 part_num) {
  // If partition number out of bounds, error.
  if (part_num >= ndm->num_partitions) {
    FsError2(NDM_CFG_ERR, EINVAL);
    return NULL;
  }

  // Return pointer to specified partition structure.
  return &ndm->partitions[part_num];
}

// ndmWritePartition: Write a partition entry into partitions table
//
//      Inputs: ndm = pointer to NDM control block
//              part = buffer to get partition information from
//              part_num = partition number
//              name = partition name
//
//     Returns: 0 on success, -1 on error
//
int ndmWritePartition(NDM ndm, const NDMPartition* part, ui32 part_num, const char* name) {
  ui32 i;

  // Ensure name is not too long and copy it to partition structure.
  if (strlen(name) >= NDM_PART_NAME_LEN)
    return FsError2(NDM_CFG_ERR, EINVAL);
  strcpy((char*)part->name, name);

  // If partition first block or number of blocks invalid, error.
  if (part->first_block >= ndm->num_vblks || part->first_block + part->num_blocks > ndm->num_vblks)
    return FsError2(NDM_CFG_ERR, EINVAL);

  // If there are partitions already, check there is no overlap.
  for (i = 0; i < ndm->num_partitions; ++i) {
    // Skip partition we want to replace.
    if (i == part_num)
      continue;

    // Check for overlap only against valid partition entries.
    if (ndm->partitions[i].type) {
      if ((part->first_block >= ndm->partitions[i].first_block &&
           part->first_block < ndm->partitions[i].first_block + ndm->partitions[i].num_blocks) ||
          (ndm->partitions[i].first_block >= part->first_block &&
           ndm->partitions[i].first_block < part->first_block + part->num_blocks))
        return FsError2(NDM_CFG_ERR, EINVAL);
    }
  }

  // If partition number out of bounds, adjust partition table.
  if (part_num >= ndm->num_partitions)
    if (ndmSetNumPartitions(ndm, part_num + 1))
      return -1;

  // Write partition information and return success.
  memcpy(&ndm->partitions[part_num], part, sizeof(NDMPartition));
  return 0;
}

// ndmEraseBlock: Erase a block
//
//      Inputs: vpn = base virtual page for block to be erased
//              ndm = NDM control block handle
//
//     Returns: 0 on success, -1 on fatal error
//
int ndmEraseBlock(ui32 vpn, NDM ndm) {
  ui32 vbn, bn, pn;
  int status;

  // Compute the virtual block number based on virtual page number.
  vbn = vpn / ndm->pgs_per_blk;
  if (vbn >= ndm->num_vblks)
    return FsError2(NDM_ASSERT, EINVAL);

  // Grab exclusive access to TargetNDM internals.
  semPend(ndm->sem, WAIT_FOREVER);

  // Get the physical block number from virtual one.
  bn = get_pbn(ndm, vbn, WR_MAPPING);
  if (bn == (ui32)-1) {
    semPostBin(ndm->sem);
    return -1;
  }

  // Compute physical page number.
  pn = bn * ndm->pgs_per_blk + vpn % ndm->pgs_per_blk;

  // Erase the block.
  status = ndm->erase_block(pn, ndm->dev);
  if (status < 0) {
    // If chip error, do bad block recovery. Else return fatal error.
    if (status == -1)
      status = ndmMarkBadBlock(ndm, bn, ERASE_BLOCK);
    else
      FsError2(NDM_EIO, EIO);
  }

  // Release exclusive NDM access and return status.
  semPostBin(ndm->sem);
  return status;
}

// ndmDeletePartitionTable: Delete partition table
//
//       Input: ndm = pointer to NDM control block
//
void ndmDeletePartitionTable(NDM ndm) {
  // If no partitions, simply return.
  if (ndm->num_partitions == 0)
    return;

  // Remove partition table.
  FsFreeClear(&ndm->partitions);
  ndm->num_partitions = 0;
}

// ndmSavePartitionTable: Save partition table to flash
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on error
//
int ndmSavePartitionTable(NDM ndm) {
  ndm->xfr_tblk = (ui32)-1;
  return ndmWrCtrl(ndm);
}  // lint !e818

#if BBL_INSERT_INC
// ndmExtractBBL: Save running bad block list count and data
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: -1 if error, else number of running bad blocks
//
int ndmExtractBBL(NDM ndm) {
  uint size;
  Pair *pair, *tmp, *last_pair;

  // Save running bad block count. Return if there are none.
  ExtractedCnt = ndm->num_rbb;
  if (ExtractedCnt == 0)
    return 0;

  // Allocate memory for running bad block list and copy list to it.
  size = sizeof(Pair) * ExtractedCnt;
  ExtractedList = malloc(size);
  if (ExtractedList == NULL)
    return FsError2(NDM_ENOMEM, ENOMEM);
  memcpy(ExtractedList, ndm->run_bad_blk, size);
#if BBL_INSERT_DEBUG
  show_rbbl(ndm, ndm->run_bad_blk, ndm->num_rbb);
  show_rbbl(ndm, ExtractedList, ExtractedCnt);
#endif

  // Simplify list, eliminating chains.
  pair = ExtractedList;
  for (last_pair = pair + ExtractedCnt - 1; pair < last_pair; ++pair) {
    if (pair->val != (ui32)-1) {
      for (tmp = pair + 1; tmp <= last_pair; ++tmp) {
        if (tmp->key == pair->val) {
          pair->val = tmp->val;
          tmp->val = (ui32)-1;
        }
      }
    }
  }
#if BBL_INSERT_DEBUG
  show_rbbl(ndm, ExtractedList, ExtractedCnt);
#endif

  // Return number of blocks that failed so far.
  return ExtractedCnt;
}

// ndmInsertBBL: Import saved running bad block list
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 if successful, else -1 on error
//
int ndmInsertBBL(NDM ndm) {
  ui32 free_b, old_pn, new_pn, past_end;
  Pair *last_pair, *pair;
  int action, rc;

  // Just return 0 if there were no running bad blocks.
  if (ExtractedCnt == 0)
    return 0;

  // Loop to recover each bad block.
  pair = ExtractedList;
  for (last_pair = pair + ExtractedCnt - 1; pair <= last_pair; ++pair) {
#if BBL_INSERT_DEBUG
    ndm->logger.debug("pair %u: vblk/key=%u, pblk/val=%d", pair - ExtractedList, pair->key,
                      pair->val);
#endif

    // Adjust bad block count. If too many, error.
    if (++ndm->num_bad_blks > ndm->max_bad_blks)
      return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

    // Get a free block to replace the bad virtual block.
    free_b = get_free_virt_blk(ndm);
    if (free_b == (ui32)-1)
      return FsError2(NDM_NO_FREE_BLK, ENOSPC);

    // If physical block assigned, copy its contents to free_b.
    if (pair->val != (ui32)-1) {
      // Keep looping until successful.
      for (;;) {
        // Call driver to erase the free block.
        rc = ndm->erase_block(free_b * ndm->pgs_per_blk, ndm->dev);
        if (rc == -2)
          return FsError2(NDM_EIO, EIO);

        // Check if block erase command succeeded.
        if (rc == 0) {
          // Transfer data from old pool block to new pool block.
          old_pn = pair->val * ndm->pgs_per_blk;
          new_pn = free_b * ndm->pgs_per_blk;
          past_end = new_pn + ndm->pgs_per_blk;
          for (; new_pn < past_end; ++old_pn, ++new_pn) {
            // Call driver to see if main and spare areas are erased.
            rc = ndm->page_blank(old_pn, ndm->main_buf, ndm->spare_buf, ndm->dev);

            // If erased, skip page. Else if I/O error, return -1.
            if (rc == TRUE)
              continue;
            else if (rc < 0)
              return FsError2(NDM_EIO, EIO);

            // Read main data. Return -1 if ECC or fatal error.
            rc = ndm->read_page(old_pn, ndm->main_buf, ndm->spare_buf, ndm->dev);
            if (rc < 0)
              return FsError2(NDM_EIO, EIO);

            // Read old spare data. Return -1 if ECC or fatal error.
            rc = ndm->read_decode_spare(old_pn, ndm->spare_buf, ndm->dev);
            if (rc < 0)
              return FsError2(NDM_EIO, EIO);

            // If volume page, request just spare bytes ECC.
            if (RD32_LE(&ndm->spare_buf[5]) == (ui32)-1)
              action = NDM_ECC;

            // Else map page, request spare ECC/validity check prep.
            else
              action = NDM_ECC_VAL;

            // Write page. Return -1 if fatal err. Break if block bad.
            rc = ndm->write_page(new_pn, ndm->main_buf, ndm->spare_buf, action, ndm->dev);
            if (rc == -2)
              return -1;
            else if (rc)
              break;
          }

          // Break if block copy loop completed successfully.
          if (new_pn == past_end)
            break;
        }

        // Adjust bad block count. If too many, error.
        if (++ndm->num_bad_blks > ndm->max_bad_blks)
          return FsError2(NDM_TOO_MANY_RBAD, ENOSPC);

        // Add the failed copy block to the running bad block list.
        ndm->run_bad_blk[ndm->num_rbb].val = (ui32)-1;
        ndm->run_bad_blk[ndm->num_rbb].key = free_b;
        ++ndm->num_rbb;

        // Get a free block to replace the bad virtual block.
        free_b = get_free_virt_blk(ndm);
        if (free_b == (ui32)-1)
          return FsError2(NDM_NO_FREE_BLK, ENOSPC);
      }
    }

    // Add this extracted pair to the current running bad block list.
    ndm->run_bad_blk[ndm->num_rbb].val = free_b;
    ndm->run_bad_blk[ndm->num_rbb].key = pair->key;
    ++ndm->num_rbb;
  }
#if BBL_INSERT_DEBUG
  show_rbbl(ndm, ndm->run_bad_blk, ndm->num_rbb);
#endif

  // Free bad block list memory, zero count, and save the metadata.
  free(ExtractedList);
  ExtractedCnt = 0;
  return ndmWrCtrl(ndm);
}
#endif  // BBL_INSERT_INC

#if INC_FTL_NDM_MLC
// ndmPastPrevPair: Starting at specified page number, find first page
//              that has no earlier paired page
//
//      Inputs: ndm = pointer to NDM control block
//              pn = number of prospective next free page
//
//     Returns: first of next pages whose write failures that can't
//              corrupt previously written pages on same block or -1
//              if no higher page on block past all previous pairs
//
ui32 ndmPastPrevPair(CNDM ndm, ui32 pn) {
  ui32 n, po = pn % ndm->pgs_per_blk;

  // First page on block has no previous pairs to skip.
  if (po == 0)
    return pn;

  // If last page on block is not past previous pairs, no page is.
  n = ndm->pgs_per_blk - 1;
  if (ndm->pair_offset(n, ndm->dev) < po)
    return (ui32)-1;

  // Move backward to find first page whose pair is at a lower offset
  // than the input page.
  for (;;) {
    if (ndm->pair_offset(--n, ndm->dev) < po)
      break;
    PfAssert(n);
  }

  // Return the page that is one page offset higher.
  return (pn / ndm->pgs_per_blk) * ndm->pgs_per_blk + n + 1;
}
#endif
