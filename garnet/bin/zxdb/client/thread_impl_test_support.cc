// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl_test_support.h"

#include "garnet/bin/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

ThreadImplTest::ThreadImplTest() = default;
ThreadImplTest::~ThreadImplTest() = default;

std::unique_ptr<RemoteAPI> ThreadImplTest::GetRemoteAPIImpl() {
  auto remote_api = std::make_unique<MockRemoteAPI>();
  mock_remote_api_ = remote_api.get();
  return std::move(remote_api);
}

TestThreadObserver::TestThreadObserver(Thread* thread) : thread_(thread) {
  thread->AddObserver(this);
}

TestThreadObserver::~TestThreadObserver() { thread_->RemoveObserver(this); }

void TestThreadObserver::OnThreadStopped(
    Thread* thread, debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  EXPECT_EQ(thread_, thread);
  got_stopped_ = true;
  hit_breakpoints_ = hit_breakpoints;
}

}  // namespace zxdb
