// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/address_range.h"

#include <inttypes.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace debug_ipc {

// Implemented out-of-line to avoid bringing <algorithm> into all headers that use address_range.h.
AddressRange AddressRange::Union(const AddressRange& other) const {
  if (other.empty())
    return *this;
  if (empty())
    return other;
  return AddressRange(std::min(begin_, other.begin_), std::max(end_, other.end_));
}

std::string AddressRange::ToString() const {
  return fxl::StringPrintf("[0x%" PRIx64 ", 0x%" PRIx64 ")", begin_, end_);
}

}  // namespace debug_ipc
