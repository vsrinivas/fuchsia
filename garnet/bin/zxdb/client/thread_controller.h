// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/common/address_range.h"

namespace zxdb {

class Breakpoint;
class Err;
class Frame;
class Location;
class Thread;

// Uncomment to enable detailed thread controller logging.
//
// TODO(brettw) when we have a settings system, make this run-time enableable
// for easier debugging when people encounter problems in the field.
//
// #define DEBUG_THREAD_CONTROLLERS

// Abstract base class that provides the policy decisions for various types of
// thread stepping.
class ThreadController {
 public:
  enum StopOp {
    // Resume the thread. A controller can indicate "continue" but if another
    // indicates "stop", the "stop" will take precedence.
    kContinue,

    // Keeps the thread stopped and reports the stop to the user. The
    // controller is marked done and should be deleted. This takes precedence
    // over any "continue" votes.
    kStopDone,

    // Reports that the controller doesn't know what to do with this thread
    // stop. This is effectively a neutral vote for what should happen in
    // response to a thread stop. If all active controllers report
    // "unexpected", the thread will stop.
    kUnexpected
  };

  // How the thread should run when it is executing this controller.
  struct ContinueOp {
    // Factory helper functions.
    static ContinueOp Continue() {
      return ContinueOp();  // Defaults are good for this case.
    }
    static ContinueOp StepInstruction() {
      ContinueOp result;
      result.how = debug_ipc::ResumeRequest::How::kStepInstruction;
      return result;
    }
    static ContinueOp StepInRange(AddressRange range) {
      ContinueOp result;
      result.how = debug_ipc::ResumeRequest::How::kStepInRange;
      result.range = range;
      return result;
    }
    // See synthetic_stop_ below.
    static ContinueOp SyntheticStop() {
      ContinueOp result;
      result.synthetic_stop_ = true;
      return result;
    }

    // A synthetic stop means that the thread remains stopped but a synthetic
    // stop notification is broadcast to make it look like the thread did
    // continued and stopped again. This will call back into the top
    // controller's OnThreadStop().
    //
    // This is useful when modifying the stack for inline routines, where the
    // code didn't execute but from a user perspective they stepped into an
    // inline subroutine. In this case the thread controller will update the
    // Stack to reflect the new state, and return ContinueOp::SyntheticStop().
    //
    // Why isn't this a StopOp instead? This only makes sense as the initial
    // state of the ThreadController that decides it doesn't need to do
    // anything but wants to pretend that it did. When a ThreadController is in
    // OnThreadStop and about to return a StopOp, returning kStop is a real
    // thread stop and nothing needs to be synthetic.
    //
    // See GetContinueOp() for more.
    bool synthetic_stop_ = false;

    // Valid when synthetic_stop = true.
    debug_ipc::ResumeRequest::How how =
        debug_ipc::ResumeRequest::How::kContinue;

    // When how == kStepInRange, this defines the address range to step in. As
    // long as the instruction pointer is inside, execution will continue.
    AddressRange range;
  };

  ThreadController();

  virtual ~ThreadController();

  // Registers the thread with the controller. The controller will be owned
  // by the thread (possibly indirectly) so the pointer will remain valid for
  // the rest of the lifetime of the controller.
  //
  // The implementation should call set_thread() with the thread.
  //
  // When the implementation is ready, it will issue the given callback to
  // run the thread. The callback can be issued reentrantly from inside this
  // function if the controller is ready synchronously.
  //
  // If the callback does not specify an error, the thread will be resumed
  // when it is called. If the callback has an error, it will be reported and
  // the thread will remain stopped.
  virtual void InitWithThread(Thread* thread,
                              std::function<void(const Err&)> cb) = 0;

  // Returns how to continue the thread when running this controller. This
  // will be called after InitWithThread and after every subsequent kContinue
  // response from OnThreadStop to see how the controller wishes to run.
  //
  // A thread controller can return a "synthetic stop" from this function which
  // will schedule an OnThreadStop() call in the future without running the
  // thread. This can be used to adjust the ambiguous inline stack state (see
  // Stack object) to implement step commands.
  //
  // GetContinueOp() should not change thread state and controllers should be
  // prepared for only InitWithThread() followe by OnThreadStop() calls. When
  // thread controllers embed other thread controllers, the embedding
  // controller may create the nested one and want it to evaluate the current
  // stop, and this happens without ever continuing.
  virtual ContinueOp GetContinueOp() = 0;

  // Notification that the thread has stopped. The return value indicates what
  // the thread should do in response.
  //
  // If the ThreadController returns |kStop|, its assumed the controller has
  // completed its job and it will be deleted. |kContinue| doesn't necessarily
  // mean the thread will continue, as there could be multiple controllers
  // active and any of them can report "stop". When a thread is being
  // continued, the main controller will get GetContinueOp() called to see what
  // type of continuation it wants.
  virtual StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) = 0;

#if defined(DEBUG_THREAD_CONTROLLERS)
  // Writes the log message prefixed with the thread controller type. Callers
  // should pass constant strings through here so the Log function takes
  // almost no time if it's disabled: in the future we may want to make this
  // run-time enable-able
  void Log(const char* format, ...) const;

  // Logs the raw string (no controller name prefix).
  static void LogRaw(const char* format, ...);

  // Returns the given frame's function name or a placeholder string if
  // unavailable. Does nothing if logging is disabled (computing this is
  // non-trivial).
  static std::string FrameFunctionNameForLog(const Frame* frame);
#else
  void Log(const char* format, ...) const {}
  static void LogRaw(const char* format, ...) {}
  static std::string FrameFunctionNameForLog(const Frame* frame) {
    return std::string();
  }
#endif

 protected:
  // How the frame argument to SetInlineFrameIfAmbiguous() is interpreted.
  enum class InlineFrameIs {
    // Set the inline frame equal to the given one.
    kEqual,

    // Set the inline frame to the frame immediately before the given one. This
    // exists so that calling code can reference the previuos frame without
    // actually having to compute the fingerprint of the previous frame (it may
    // not be available if previous stack frames haven't been synced).
    kOneBefore
  };

  Thread* thread() { return thread_; }
  void set_thread(Thread* thread) { thread_ = thread; }

  // Returns the name of this thread controller. This will be visible in logs.
  // This should be something simple and short like "Step" or "Step Over".
  virtual const char* GetName() const = 0;

  // The beginning of an inline function is ambiguous about whether you're at
  // the beginning of the function or about to call it (see Stack object for
  // more).
  //
  // Many stepping functions know what frame they think they should be in, and
  // identify this based on the frame fingerprint. As a concrete example, if
  // a "finish" command exits a stack frame, but the next instruction is the
  // beginning of an inlined function, the "finish" controller would like to
  // say you're in the stack it returned to, not the inlined function.
  //
  // This function checks if there is ambiguity of inline frames and whether
  // one of those ambiguous frames matches the given fingerprint. In this case,
  // it will set the top stack frame to be the requested one.
  //
  // If there is no ambiguity or one of the possibly ambiguous frames doesn't
  // match the given fingerprint, the inline frame hide count will be
  // unchanged.
  void SetInlineFrameIfAmbiguous(InlineFrameIs comparison,
                                 FrameFingerprint fingerprint);

  // Tells the owner of this class that this ThreadController has completed
  // its work. Normally returning kStop from OnThreadStop() will do this, but
  // if the controller has another way to get events (like breakpoints), it
  // may notice out-of-band that its work is done.
  //
  // This function will likely cause |this| to be deleted.
  void NotifyControllerDone();

 private:
  Thread* thread_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadController);
};

}  // namespace zxdb
