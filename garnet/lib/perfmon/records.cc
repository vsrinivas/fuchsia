// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "records.h"

#include <src/lib/fxl/arraysize.h>

namespace perfmon {

perfmon_record_type_t RecordType(const perfmon_record_header_t* hdr) {
  switch (hdr->type) {
    case PERFMON_RECORD_TIME:
    case PERFMON_RECORD_TICK:
    case PERFMON_RECORD_COUNT:
    case PERFMON_RECORD_VALUE:
    case PERFMON_RECORD_PC:
    case PERFMON_RECORD_LAST_BRANCH:
      return static_cast<perfmon_record_type_t>(hdr->type);
    default:
      return PERFMON_RECORD_RESERVED;
  }
}

size_t RecordSize(const perfmon_record_header_t* hdr) {
  switch (hdr->type) {
    case PERFMON_RECORD_TIME:
      return sizeof(perfmon_time_record_t);
    case PERFMON_RECORD_TICK:
      return sizeof(perfmon_tick_record_t);
    case PERFMON_RECORD_COUNT:
      return sizeof(perfmon_count_record_t);
    case PERFMON_RECORD_VALUE:
      return sizeof(perfmon_value_record_t);
    case PERFMON_RECORD_PC:
      return sizeof(perfmon_pc_record_t);
    case PERFMON_RECORD_LAST_BRANCH: {
      auto rec = reinterpret_cast<const perfmon_last_branch_record_t*>(hdr);
      if (rec->num_branches > arraysize(rec->branches))
        return 0;
      return PERFMON_LAST_BRANCH_RECORD_SIZE(rec);
    }
    default:
      return 0;
  }
}

}  // namespace perfmon
