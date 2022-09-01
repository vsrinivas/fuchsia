// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnose.h"

#include <functional>
#include <string_view>

#include "src/storage/lib/ftl/ndm/ndmp.h"

namespace {

struct KnownIssue {
  // Diagnostic function that returns true if a known issue is found, or false otherwise.
  std::function<bool(FTLN)> diagnostic;
  // Error message that should be displayed if the diagnostic function returns true.
  std::string_view error_message;
};

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
    if (ndmReadSpare(ftl->mpns[i], ftl->spare_buf, static_cast<NDM>(ftl->ndm)) < 0) {
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

// Ensures that all mapped vpns point to a physical page that is designated as that vpn.
bool PrematureBlockRecycle(FTLN ftl) {
  bool overlap = false;
  for (uint32_t vpn = 0; vpn < ftl->num_vpages; ++vpn) {
    uint32_t ppn;
    if (FtlnMapGetPpn(ftl, vpn, &ppn) < 0 || ppn == UINT32_MAX) {
      continue;
    }
    if (ndmReadSpare(ppn, ftl->spare_buf, static_cast<NDM>(ftl->ndm)) < 0) {
      fprintf(stderr, "Failed to read spare for ppn %u\n", ppn);
      continue;
    }
    if (GET_SA_VPN(ftl->spare_buf) != vpn) {
      overlap = true;
    }
  }

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

// Checks to see if the total bad blocks on the volume equal the maximum bad blocks, and will
// thus fail to progress if one more is found. Since that additional bad block will never be
// recorded it is possible that this is the cause of failure for a disk image when run on the
// original device.
bool OutOfSpareBlocks(FTLN ftl) {
  ndm* n = reinterpret_cast<ndm*>(ftl->ndm);
  if (n->num_bad_blks >= n->max_bad_blks) {
    uint32_t initial_bad_blocks = n->num_bad_blks - n->num_rbb;
    fprintf(stderr, "Maximum %u bad blocks. Found %u bad blocks. %u initial and %u running.\n",
            n->max_bad_blks, n->num_bad_blks, initial_bad_blocks, n->num_rbb);
    return true;
  }
  return false;
}

}  // namespace

namespace ftl {

std::string FtlnDiagnoseIssues(FTLN ftl) {
  KnownIssue issues[] = {
      {
          &PartialPageWrites,
          "Block count in the billions. Partial Page Writes occurred. fxbug.dev/87629\n",
      },
      {
          &PartialPageWritesWithFix,
          "Found Partial Page Writes despite the fix being present.\n",
      },
      {
          &PrematureBlockRecycle,
          "A vpage points to a physical page which contains a different vpage. Premature Block "
          "Recycles occurred. fxbug.dev/87653\n",
      },
      {
          &LostMapBlock,
          "Unmapped map pages. An in-use map block may have been deleted. fxbug.dev/88465\n",
      },
      {
          &OutOfSpareBlocks,
          "No more spare blocks available in ndm.\n",
      }};

  std::string analysis_result;

  for (const KnownIssue& known_issue : issues) {
    if (known_issue.diagnostic(ftl)) {
      analysis_result.append(known_issue.error_message);
    }
  }

  return analysis_result;
}

}  // namespace ftl
