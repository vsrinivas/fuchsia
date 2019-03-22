// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {

// Collection of utilities functions and classes for dealing with records.

// AddressRange ----------------------------------------------------------------

bool Equals(const AddressRange &, const AddressRange &);
std::string AddressRangeToString(const AddressRange &);

// Used only for testing, collisions are bound to be very unlikely.
struct AddressRangeCompare {
  bool operator()(const AddressRange &, const AddressRange &) const;
};

}  // namespace debug_ipc
