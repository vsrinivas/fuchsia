// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ALIGN_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ALIGN_H_

#include <optional>

#include "src/developer/debug/shared/address_range.h"

namespace debug_agent {

// Depending on their size, watchpoints can only be inserted into aligned ranges. The alignment is
// as follows:
//
//   Size Alignment
//      1    1 byte
//      2    2 byte
//      4    4 byte
//      8    8 byte
//
// A given range could be un-aligned (eg. observe two bytes unaligned). This will attempt to create
// a bigger range that will cover that range, so that the watchpoint can be installed and still
// track this range.
//
// If the range cannot be aligned (eg. unaligned 8 byte range), it will return a null option.
std::optional<debug::AddressRange> AlignRange(const debug::AddressRange& range);

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ALIGN_H_
