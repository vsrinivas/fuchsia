// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_THREAD_CONTROLLER_H_

#include "src/developer/debug/zxdb/client/function_step.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"

namespace zxdb {

// The function thread controller handles the different options for how we might transparently
// handle a function call. It will dispatch to different operations:
//  - It might step through PLT stubs.
//  - It might step out of standard library calls.
//  - It might step out of unsymbolized functions.
class FunctionThreadController : public ThreadController {
 public:
  explicit FunctionThreadController(FunctionStep mode, fit::deferred_callback on_done = {});

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Function Step"; }

 private:
  FunctionStep mode_;

  // If set, this controller has been instantiated to execute the function stepping mode.
  std::unique_ptr<ThreadController> sub_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_THREAD_CONTROLLER_H_
