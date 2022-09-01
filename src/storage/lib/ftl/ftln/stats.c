// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl.h"
#include "ftlnp.h"

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
      if (ftln_debug() <= 1) {
        flush_bstat(ftl, &vol0, &vole, b, "VOLUME");
      } else {
        printf("B = %4u - used = %2u, wc lag = %3d, rc = %8u - ", b, NUM_USED(ftl->bdata[b]),
               ftl->blk_wc_lag[b], GET_RC(ftl->bdata[b]));
        printf("VOLUME BLOCK\n");
      }
    }
  }
  flush_bstat(ftl, &free0, &freee, -1, "FREE");
  flush_bstat(ftl, &vol0, &vole, -1, "VOLUME");
}

//   FtlnStats: Display FTL statistics
//
//       Input: ftl = pointer to FTL control block
//
void FtlnStats(FTLN ftl) {
  ui32 b, n;

  printf("\nFTL STATS:\n");
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
