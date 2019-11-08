// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_

#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

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

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_HELPERS_H_
