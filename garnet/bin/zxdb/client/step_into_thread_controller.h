// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/step_mode.h"
#include "garnet/bin/zxdb/client/step_thread_controller.h"

namespace zxdb {

// Implements conceptual "step into" operation from the user's perspective.
// Use this when the user says "step into", but use the StepThreadController
// in all other cases (e.g. as a component of "step over"). The difference
// is in how inlined functions are handled.
//
// When the user is at the first instruction of an inlined subroutine, the
// instruction is ambiguous about whether it's in the imaginary inline frame
// we synthesize to make it look like a function call, or at the imaginary call
// site of that frame. In this case, the Stack can be set to be in a specific
// one of these ambiguous frames by other thread controllers.
//
// When the user is at the imaginary call instruction of an inlined routine,
// "step over" will skip the inlined code, and "step into" goes into the
// imaginary inlined frame. The critical thing here is that this "step into"
// does not change the instruction pointer, it only changes the inlined frame
// to pretend to be in the inlined routine now.
//
// This controller exists only to manage that transition into inlined functions
// where the stack state is modified without affecting the CPU. It will result
// in a synthetic thread stop operation which is what the user wants, but not
// what any other thread controller expects.
class StepIntoThreadController : public ThreadController {
 public:
  // This doesn't currently support "AddressRange" mode because that's not
  // something the user specifies.
  explicit StepIntoThreadController(StepMode mode);

  ~StepIntoThreadController();

  // Forwarded to StepThreadController when it's created. See that class's
  // version of this setter for more.
  void set_stop_on_no_symbols(bool stop) { stop_on_no_symbols_ = stop; }

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step into"; }

 private:
  // Attempts to step into an inline function that starts and the current
  // stack addresss. This will make it look like the user stepped into the
  // inline function even though no code was executed.
  //
  // If there is an inline to step into, this will fix up the current stack to
  // appear as if the inline function is stepped into and return true. False
  // means there was not an inline function starting at the current address.
  bool TrySteppingIntoInline();

  StepMode mode_;

  // Temporary storage for the flag to StepThreadController that controls
  // whether executing code with no symbols should stop or not. See
  // StepThreadController::stop_on_no_symbols_.
  bool stop_on_no_symbols_ = false;

  // Performs the normal CPU stepping in a code range when we're not doing
  // the special step-into-inline-function case.
  std::unique_ptr<StepThreadController> step_controller_;
};

}  // namespace zxdb
