// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_TRACE_H_
#define SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_TRACE_H_

#include <stdint.h>

namespace fs {

// Generates a trace ID that will be unique across the system (barring overflow of the per-process
// nonce, reuse of a zx_handle_t for two processes, or some other code in this process which uses
// the same procedure to generate IDs).
//
// We use this instead of the standard TRACE_NONCE because TRACE_NONCE is only unique within a
// process; we need IDs that are unique across all processes.
uint64_t GenerateTraceId();

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_TRACE_H_
