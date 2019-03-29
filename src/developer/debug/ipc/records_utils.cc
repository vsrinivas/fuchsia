// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/records_utils.h"

#include <inttypes.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace debug_ipc {

bool Equals(const debug_ipc::AddressRange& lhs,
            const debug_ipc::AddressRange& rhs) {
  return lhs.begin == rhs.begin && lhs.end == rhs.end;
}

std::string AddressRangeToString(const debug_ipc::AddressRange& range) {
  return fxl::StringPrintf("Begin: 0x%" PRIx64 ", End 0x%" PRIx64, range.begin,
                           range.end);
}

// Used only for testing, collisions are bound to be very unlikely.
bool AddressRangeCompare::operator()(const debug_ipc::AddressRange& lhs,
                                     const debug_ipc::AddressRange& rhs) const {
  if (lhs.begin != rhs.begin)
    return lhs.begin < rhs.begin;
  return lhs.end < rhs.end;
}

}  // namespace debug_ipc
