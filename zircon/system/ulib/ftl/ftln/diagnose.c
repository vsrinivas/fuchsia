// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ftlnp.h"

typedef struct {
  bool (*check)(FTLN ftl);
  const char* message;
  bool result;
} KnownIssue;

// A partial page write between two pages will result in the block count
// being corrupted by overwriting the most significant byte with 0xff. The
// resulting value is in the billions and unlikely to have happened
// organically.
bool PartialPageWrites(FTLN ftl) { return ftl->high_bc >= 0xff000000; }

// Check if a partial page write occurred despite having the fix present. This is possible and may
// be ok if the partial page write happened before the fix was picked up, and was on non-critical
// data. If that does happen the volume is likely to corrupt soon after by maxing out the wear value
// of a volume block, which will cause it to be interpreted as free.
bool PartialPageWritesWithFix(FTLN ftl) {
  if (!PartialPageWrites(ftl)) {
    return false;
  }
  bool found_ppw_fix = false;
  // Check the spare area of all current map pages for the fix, since map pages are always written
  // last, if it is present anywhere it should be present in one of those.
  for (uint32_t i = 0; i < ftl->num_map_pgs; ++i) {
    // Ignore unmapped map pages.
    if (ftl->mpns[i] == UINT32_MAX) {
      continue;
    }
    if (ndmReadSpare(ftl->mpns[i], ftl->spare_buf, ftl->ndm) < 0) {
      fprintf(stderr, "Failed to read map page %u at physical page %u\n", i, ftl->mpns[i]);
      break;
    }
    // This byte is set for validity checks in the fix.
    if (ftl->spare_buf[14] != 0xff) {
      found_ppw_fix = true;
      break;
    }
  }

  return found_ppw_fix;
}

// Create a reverse mapping from the map pages to find virtual pages that share
// a physical page.
bool PrematureBlockRecycle(FTLN ftl) {
  uint32_t phys_pages = ftl->num_pages;
  uint32_t* reverse_mapping = malloc(sizeof(uint32_t) * phys_pages);

  // UINT32_MAX is used as the placeholder for unmapped entries in the ftl,
  // doing the same here.
  memset(reverse_mapping, 0xff, sizeof(uint32_t) * phys_pages);

  bool overlap = false;
  for (uint32_t vpn = 0; vpn < ftl->num_vpages; ++vpn) {
    uint32_t ppn;
    if (FtlnMapGetPpn(ftl, vpn, &ppn) < 0 || ppn == UINT32_MAX) {
      continue;
    }
    if (reverse_mapping[ppn] != UINT32_MAX) {
      overlap = true;
      break;
    }
    reverse_mapping[ppn] = vpn;
  }

  free(reverse_mapping);
  return overlap;
}

// Step through the current map pages and spot a gap in mappings. This isn't necessarily a bad
// thing, but it means that there are large gaps in the middle of volume, which are unlikely to be
// normal occurrences in our use case. This should only happen naturally if a region has *never*
// been written to. Trimming it all will create an empty map page, not unmap the map page.
bool LostMapBlock(FTLN ftl) {
  bool found_empty = false;
  // The map page number is the meta-page marker, we don't care about those.
  for (uint32_t mpn = 0; mpn < ftl->num_map_pgs - 1; ++mpn) {
    // Unmapped map pages are marked with all 0xFF.
    if (ftl->mpns[mpn] == UINT32_MAX) {
      found_empty = true;
    } else if (found_empty) {
      return true;
    }
  }
  return false;
}

//  FtlnDiagnoseIssues: Search for known bad symptoms in an FTL.
//
//       Input: ftl = pointer to fully mounted FTL control block.
//
//     Returns: NULL when no symptoms found, or a char* to a human readable
//              message. Caller is responsible for freeing.
//
char* FtlnDiagnoseIssues(FTLN ftl) {
  KnownIssue issues[] = {
      {
          &PartialPageWrites,
          "Block count in the billions. Partial Page Writes occured. fxbug.dev/87629\n",
          false,
      },
      {
          &PartialPageWritesWithFix,
          "Found Partial Page Writes despite the fix being present.\n",
          false,
      },
      {
          &PrematureBlockRecycle,
          "Two vpages share a physical page. Premature Block Recycles occured. fxbug.dev/87653\n",
          false,
      },
      {
          &LostMapBlock,
          "Unmapped map pages. An in-use map block may have been deleted. fxbug.dev/88465\n",
          false,
      },
  };

  size_t issue_count = sizeof(issues) / sizeof(*issues);
  size_t message_length = 0;

  for (size_t i = 0; i < issue_count; ++i) {
    issues[i].result = issues[i].check(ftl);
    if (issues[i].result) {
      message_length += strlen(issues[i].message);
    }
  }

  if (message_length == 0) {
    return NULL;
  }

  char* analysis = malloc(sizeof(char*) * (message_length + 1));
  analysis[0] = '\0';
  for (size_t i = 0; i < issue_count; ++i) {
    if (issues[i].result) {
      strcat(analysis, issues[i].message);
    }
  }

  return analysis;
}
