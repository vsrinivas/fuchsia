// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/backtrace_cache.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_impl_test_support.h"
#include "src/developer/debug/zxdb/symbols/location.h"

using namespace debug_ipc;

namespace zxdb {

class BacktraceCacheControllerTest : public InlineThreadControllerTest {};

TEST_F(BacktraceCacheControllerTest, DoCache) {
  BacktraceCache cache;
  cache.set_should_cache(true);
  thread()->AddObserver(&cache);

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           NotifyException::Type::kSoftware,
                           MockFrameVectorToFrameVector(GetStack()), true);

  EXPECT_EQ(cache.backtraces().size(), 1u);

  auto stack = GetStack();
  auto& backtrace = cache.backtraces().back();
  ASSERT_EQ(backtrace.frames.size(), stack.size());
  for (size_t i = 0; i < backtrace.frames.size(); i++) {
    auto& frame = backtrace.frames[i];
    auto& mock_frame = stack[i];
    EXPECT_EQ(frame.file_line, mock_frame->GetLocation().file_line());
  }
}

}  // namespace zxdb
