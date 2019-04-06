// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "records.h"

#include <src/lib/fxl/arraysize.h>

namespace perfmon {

RecordType GetRecordType(const RecordHeader* hdr) {
  switch (hdr->type) {
    case kRecordTypeTime:
    case kRecordTypeTick:
    case kRecordTypeCount:
    case kRecordTypeValue:
    case kRecordTypePc:
    case kRecordTypeLastBranch:
      return static_cast<RecordType>(hdr->type);
    default:
      return kRecordTypeInvalid;
  }
}

size_t GetRecordSize(const RecordHeader* hdr) {
  switch (hdr->type) {
    case kRecordTypeTime:
      return sizeof(TimeRecord);
    case kRecordTypeTick:
      return sizeof(TickRecord);
    case kRecordTypeCount:
      return sizeof(CountRecord);
    case kRecordTypeValue:
      return sizeof(ValueRecord);
    case kRecordTypePc:
      return sizeof(PcRecord);
    case kRecordTypeLastBranch: {
      auto rec = reinterpret_cast<const LastBranchRecord*>(hdr);
      if (rec->num_branches > arraysize(rec->branches))
        return 0;
      return LastBranchRecordSize(rec);
    }
    default:
      return 0;
  }
}

}  // namespace perfmon
