// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TEST_THREAD_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TEST_THREAD_OBSERVER_H_

// This file contains a test harness and helper classes for writing tests involving lower-level
// thread control such as ThreadImpl itself, and ThreadControllers.

#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"

namespace zxdb {

class TestThreadObserver : public ThreadObserver {
 public:
  explicit TestThreadObserver(Thread* thread);
  ~TestThreadObserver();

  bool got_stopped() const { return got_stopped_; }
  void set_got_stopped(bool s) { got_stopped_ = s; }

  const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints() { return hit_breakpoints_; }

  void OnThreadStopped(Thread* thread, debug_ipc::ExceptionType type,
                       const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  Thread* thread_;

  bool got_stopped_ = false;
  std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TEST_THREAD_OBSERVER_H_
