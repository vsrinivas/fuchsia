// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_

#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {
struct Register;
}

// This file contains utility code for implementing shared capabilities the architecture-specific
// code in arch_<platform>_helpers.cc files.

namespace debug_agent {
namespace arch {

// Writes the register data to the given output variable, checking that the register data is
// the same size as the output.
template <typename RegType>
zx_status_t WriteRegisterValue(const debug_ipc::Register& reg, RegType* dest) {
  if (reg.data.size() != sizeof(RegType))
    return ZX_ERR_INVALID_ARGS;
  memcpy(dest, reg.data.data(), sizeof(RegType));
  return ZX_OK;
}

// Depending on their size, watchpoints can only be inserted into aligned ranges. The alignment is
// as follows:
//
// Size Alignment
//    1    1 byte
//    2    2 byte
//    4    4 byte
//    8    8 byte
//
// A given range could be un-aligned (eg. observe two bytes unaligned). This will attempt to create
// a bigger range that will cover that range, so that the watchpoint can be installed and still
// track this range.
//
// If the range cannot be aligned (eg. unaligned 8 byte range), it will return a null option.
std::optional<debug_ipc::AddressRange> AlignRange(const debug_ipc::AddressRange&);

// If |status| != ZX_OK, all other fields are invalid.
struct WatchpointInstallationResult {
  zx_status_t status = ZX_ERR_INVALID_ARGS;
  debug_ipc::AddressRange installed_range = {};
  int slot = -1;
};

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_
