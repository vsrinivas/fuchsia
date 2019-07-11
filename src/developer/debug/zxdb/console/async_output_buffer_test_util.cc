// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/async_output_buffer_test_util.h"

#include "src/developer/debug/shared/message_loop.h"

namespace zxdb {

OutputBuffer LoopUntilAsyncOutputBufferComplete(fxl::RefPtr<AsyncOutputBuffer> buffer) {
  if (!buffer->is_complete()) {
    // Need to wait for async completion.
    buffer->SetCompletionCallback([]() { debug_ipc::MessageLoop::Current()->QuitNow(); });
    debug_ipc::MessageLoop::Current()->Run();
  }
  return buffer->DestructiveFlatten();
}

}  // namespace zxdb
