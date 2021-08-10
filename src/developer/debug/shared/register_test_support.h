// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_REGISTER_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_SHARED_REGISTER_TEST_SUPPORT_H_

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/shared/register_value.h"

namespace debug {

// Creates a register with a data pattern within it. The pattern will 0x010203 ... (little-endian).
RegisterValue CreateRegisterWithTestData(RegisterID id, size_t length);

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_REGISTER_TEST_SUPPORT_H_
