// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/step_mode.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/symbols/file_line.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"

namespace zxdb {

class FinishThreadController;

// Implements a "step into" command. It knows how to step by source lines,
// over a range of addresses, or by single instruction.
//
// This is the main low-level thread controller used by other ones. Generally
// programmatic uses (e.g. from with "step over") will use this class.
//
// When stepping by file/line, this class will generate synthetic exceptions
// and adjust the stack to simulate stepping into inline function calls (even
// though there is no actual call instruction).
class StepThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize
  // itself to the thread's current position when the thread is attached.
  explicit StepThreadController(StepMode mode);

  // Constructor for a kAddressRange mode (the mode is implicit). Continues
  // execution as long as the IP is in range.
  explicit StepThreadController(AddressRanges ranges);

  ~StepThreadController() override;

  // Controls whether the thread will stop when it encounters code with no
  // symbols. When false, if a function is called with no symbols, it will
  // automatically step out or through it.
  //
  // This only affects "step by line" mode which is symbol-aware.
  bool stop_on_no_symbols() const { return stop_on_no_symbols_; }
  void set_stop_on_no_symbols(bool stop) { stop_on_no_symbols_ = stop; }

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step"; }

 private:
  enum class StepIntoInline {
    // Actually performs the inline step, modifying the hidden ambiguous Stack
    // items as necessary.
    kCommit,

    // Does the operations to compute whether an inline step can be completed
    // and returns the corresponding result, but does not actually change any
    // state.
    kQuery
  };

  // Attempts to step into an inline function that starts and the current
  // stack addresss. This will make it look like the user stepped into the
  // inline function even though no code was executed.
  //
  // If there is an inline to step into, this will fix up the current stack to
  // appear as if the inline function is stepped into and return true. False
  // means there was not an inline function starting at the current address.
  bool TrySteppingIntoInline(StepIntoInline command);

  // Version of OnThreadStop that handles the case where the current code has
  // no line information.
  StopOp OnThreadStopOnUnsymbolizedCode();

  StepMode step_mode_;

  // When construction_mode_ == kSourceLine, this represents the line
  // information and the stack fingerprint of where stepping started.
  FileLine file_line_;
  FrameFingerprint original_frame_fingerprint_;

  // Range of addresses we're currently stepping in. This may change when we're
  // stepping over source lines and wind up in a region with no line numbers.
  // It will be empty when stepping by instruction.
  AddressRanges current_ranges_;

  bool stop_on_no_symbols_ = false;

  // Used to step out of unsymbolized functions. When non-null, the user wants
  // to skip unsymbolized code and has stepped into an unsymbolized function.
  std::unique_ptr<FinishThreadController> finish_unsymolized_function_;
};

}  // namespace zxdb
