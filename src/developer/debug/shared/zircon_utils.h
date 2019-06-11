// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_UTILS_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_UTILS_H_

#include <stdint.h>
#include <zircon/syscalls/exception.h>

namespace debug_ipc {

const char* ExceptionTypeToString(uint32_t type);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_UTILS_H_
