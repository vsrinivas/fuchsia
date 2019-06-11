// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ZX_STATUS_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ZX_STATUS_H_

#include <stdint.h>

#include "src/developer/debug/shared/zx_status_definitions.h"

namespace debug_ipc {

const char* ZxStatusToString(zx_status_t status);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ZX_STATUS_H_
