// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_WRITER_H_
#define GARNET_LIB_PERFMON_WRITER_H_

#include <cstddef>
#include <cstdint>

namespace perfmon {

// struct to export |LastBranchRecord| as a "blob" in the trace
// format. A problem that we need to solve is giving the reader a way to match
// last branch records with their originating event. The way we do this is to
// add the cpu and timestamp to the data.

struct LastBranchRecordBlob {
  // The cpu this event was captured on.
  uint16_t cpu;
  // The number of entries in |branches|.
  uint16_t num_branches;
  // For alignment purposes, and future-proofing. Always zero.
  uint32_t reserved;
  // The time the record was obtained, in "trace ticks".
  uint64_t event_time;
  // The address space id (e.g., CR3) at the time data was collected.
  // This is not necessarily the aspace id of each branch. S/W will need to
  // determine from the branch addresses how far back aspace is valid.
  uint64_t aspace;
  // Set of branches, in reverse chronological order.
  struct LastBranchEntry {
    uint64_t from;
    uint64_t to;
    // Processor-provided details on this branch.
    // bits 0-15: Elapsed time since the last branch. Zero if unknown.
    //            The unit of measurement is processor-specific.
    // bit 16: Non-zero if branch was mispredicted.
    uint64_t info;
  } branches[];
};

static inline size_t LastBranchRecordBlobSize(uint16_t num_branches) {
  return (sizeof(LastBranchRecordBlob) +
          (num_branches * sizeof(LastBranchRecordBlob::LastBranchEntry)));
}

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_WRITER_H_
