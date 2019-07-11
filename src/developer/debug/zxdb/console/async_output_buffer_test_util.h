// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_TEST_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_TEST_UTIL_H_

#include "src/developer/debug/zxdb/console/async_output_buffer.h"

namespace zxdb {

// Runs the current message loop (which must already be set up on the current thread) until the
// AsyncOutputBuffer is complete, and returns the result.
//
// This should only be used in test code since it runs a nested message loop. For non-test code
// normally you would do Console->Output() and it will get automatically written when the buffer is
// complete.
OutputBuffer LoopUntilAsyncOutputBufferComplete(fxl::RefPtr<AsyncOutputBuffer> buffer);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_TEST_UTIL_H_
