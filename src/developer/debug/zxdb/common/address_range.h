// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ADDRESS_RANGE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ADDRESS_RANGE_H_

#include "src/developer/debug/shared/address_range.h"

namespace zxdb {

// Bring in the shared AddressRange struct to our namespace so we don't have to qualify everything
// just because this happens to be shared by the debug agent.
using AddressRange = ::debug::AddressRange;

using AddressRangeBeginCmp = ::debug::AddressRangeBeginCmp;
using AddressRangeEndAddrCmp = ::debug::AddressRangeEndAddrCmp;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ADDRESS_RANGE_H_
