// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "records.h"

namespace cpuperf {

cpuperf_record_type_t RecordType(const cpuperf_record_header_t* hdr) {
  switch (hdr->type) {
    case CPUPERF_RECORD_TIME:
    case CPUPERF_RECORD_TICK:
    case CPUPERF_RECORD_COUNT:
    case CPUPERF_RECORD_VALUE:
    case CPUPERF_RECORD_PC:
    case CPUPERF_RECORD_LAST_BRANCH:
      return static_cast<cpuperf_record_type_t>(hdr->type);
    default:
      return CPUPERF_RECORD_RESERVED;
  }
}

size_t RecordSize(const cpuperf_record_header_t* hdr) {
  switch (hdr->type) {
    case CPUPERF_RECORD_TIME:
      return sizeof(cpuperf_time_record_t);
    case CPUPERF_RECORD_TICK:
      return sizeof(cpuperf_tick_record_t);
    case CPUPERF_RECORD_COUNT:
      return sizeof(cpuperf_count_record_t);
    case CPUPERF_RECORD_VALUE:
      return sizeof(cpuperf_value_record_t);
    case CPUPERF_RECORD_PC:
      return sizeof(cpuperf_pc_record_t);
    case CPUPERF_RECORD_LAST_BRANCH: {
      auto rec = reinterpret_cast<const cpuperf_last_branch_record_t*>(hdr);
      if (rec->num_branches > countof(rec->branches))
        return 0;
      return CPUPERF_LAST_BRANCH_RECORD_SIZE(rec);
    }
    default:
      return 0;
  }
}

}  // namespace cpuperf
