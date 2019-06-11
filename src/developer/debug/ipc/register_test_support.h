// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_REGISTER_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_IPC_REGISTER_TEST_SUPPORT_H_

#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {

// Creates a register with the amount of data specified. The data will be zero.
Register CreateRegister(RegisterID id, size_t length);

// Creates a register with a data pattern within it.
// The pattern will 0x010203 ... (little-endian).
Register CreateRegisterWithData(RegisterID id, size_t length);

// Create a register with an uint64_t as value.
Register CreateUint64Register(RegisterID id, uint64_t value);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_REGISTER_TEST_SUPPORT_H_
