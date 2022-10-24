// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THROUGH_PLT_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THROUGH_PLT_THREAD_CONTROLLER_H_

#include <vector>

#include "src/developer/debug/zxdb/client/step_through_plt_thread_controller.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

// The POSIX dynamic loader resolves cross-library symbols with "plt" trampolines. This is a small
// bit of code in the calling module. This code calls into the dynamic loader to resolve the symbol
// on demand. The PLT address is then replaced with the destination address to avoid future lookups.
//
// These PLT trampolines are unsymbolized and users normally want to ignore them. This thread
// controller can be instantiated for the first instruction in a PLT trampoline and it will get
// through the PLT trampoline and report a stop when the destination function is reached.
//
// When InitWithThread() is called, the thread should be stopped at a PLT trampoline.
class StepThroughPltThreadController : public ThreadController {
 public:
  explicit StepThroughPltThreadController(fit::deferred_callback on_done = {});

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step Through PLT"; }

 private:
  void OnUntilControllerInitializationFailed();

  // Address of the beginning of the PLT we're at.
  uint64_t plt_address_ = 0;

  // The destination address of this PLT trampoline. There can be multiple matches if the symbol
  // lookup matches multiple locations, but one particular trampoline will match only one of these
  // addresses.
  std::vector<TargetPointer> dest_addrs_;

  // This sub-controller handles stopping the thread at the destination of the call we computed.
  //
  // We prefer the "until" controller because the trampoline could do non-trivial work (dynamically
  // resolving the destination) but fall back on single-stepping if it fails to initialize (the
  // destination of the jump isn't writable). In that failure case the until_ member will be null
  // and we'll step by instructions. Usually the PLTs are short enough where this is reasonable.
  std::unique_ptr<ThreadController> until_;

  fxl::WeakPtrFactory<StepThroughPltThreadController> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THROUGH_PLT_THREAD_CONTROLLER_H_
