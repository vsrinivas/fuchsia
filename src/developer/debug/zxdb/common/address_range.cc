// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/address_range.h"

#include <inttypes.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

AddressRange::AddressRange(uint64_t begin, uint64_t end)
    : begin_(begin), end_(end) {
  FXL_DCHECK(end_ >= begin_);
}

AddressRange AddressRange::Union(const AddressRange& other) const {
  return AddressRange(std::min(begin_, other.begin_),
                      std::max(end_, other.end_));
}

std::string AddressRange::ToString() const {
  return fxl::StringPrintf("[0x%" PRIx64 ", 0x%" PRIx64 ")", begin_, end_);
}

}  // namespace zxdb
