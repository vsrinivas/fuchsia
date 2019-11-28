// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/test_thread_observer.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

TestThreadObserver::TestThreadObserver(Thread* thread) : thread_(thread) {
  thread->session()->thread_observers().AddObserver(this);
}

TestThreadObserver::~TestThreadObserver() {
  thread_->session()->thread_observers().RemoveObserver(this);
}

void TestThreadObserver::OnThreadStopped(
    Thread* thread, debug_ipc::ExceptionType type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  EXPECT_EQ(thread_, thread);
  got_stopped_ = true;
  hit_breakpoints_ = hit_breakpoints;
}

}  // namespace zxdb
