// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread_impl.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <limits>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/frame_impl.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

ThreadImpl::ThreadImpl(ProcessImpl* process, const debug_ipc::ThreadRecord& record)
    : Thread(process->session()),
      process_(process),
      koid_(record.thread_koid),
      stack_(this),
      weak_factory_(this) {
  SetMetadata(record);
  settings_.set_fallback(&process_->target()->settings());
}

ThreadImpl::~ThreadImpl() = default;

Process* ThreadImpl::GetProcess() const { return process_; }

uint64_t ThreadImpl::GetKoid() const { return koid_; }

const std::string& ThreadImpl::GetName() const { return name_; }

debug_ipc::ThreadRecord::State ThreadImpl::GetState() const { return state_; }
debug_ipc::ThreadRecord::BlockedReason ThreadImpl::GetBlockedReason() const {
  return blocked_reason_;
}

void ThreadImpl::Pause(fit::callback<void()> on_paused) {
  // The frames may have been requested when the thread was running which will have marked them
  // "empty but complete." When a pause happens the frames will become available so we want
  // subsequent requests to request them.
  ClearFrames();

  debug_ipc::PauseRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  session()->remote_api()->Pause(
      request, [weak_thread = weak_factory_.GetWeakPtr(), on_paused = std::move(on_paused)](
                   const Err& err, debug_ipc::PauseReply reply) mutable {
        if (!err.has_error() && weak_thread) {
          // Save the new metadata.
          if (reply.threads.size() == 1 && reply.threads[0].thread_koid == weak_thread->koid_) {
            weak_thread->SetMetadata(reply.threads[0]);
          } else {
            // If the client thread still exists, the agent's record of that thread should have
            // existed at the time the message was sent so there should be no reason the update
            // doesn't match.
            FX_NOTREACHED();
          }
        }
        on_paused();
      });
}

void ThreadImpl::Continue(bool forward_exception) {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);

  if (controllers_.empty()) {
    request.how = forward_exception ? debug_ipc::ResumeRequest::How::kForwardAndContinue
                                    : debug_ipc::ResumeRequest::How::kResolveAndContinue;
  } else {
    // When there are thread controllers, ask the most recent one for how to continue.
    //
    // Theoretically we're running with all controllers at once and we want to stop at the first one
    // that triggers, which means we want to compute the most restrictive intersection of all of
    // them.
    //
    // This is annoying to implement and it's difficult to construct a situation where this would be
    // required. The controller that doesn't involve breakpoints is "step in range" and generally
    // ranges refer to code lines that will align. Things like "until" are implemented with
    // breakpoints so can overlap arbitrarily with other operations with no problem.
    //
    // A case where this might show up:
    //  1. Do "step into" which steps through a range of instructions.
    //  2. In the middle of that range is a breakpoint that's hit.
    //  3. The user does "finish." We'll ask the finish controller what to do and it will say
    //     "continue" and the range from step 1 is lost.
    // However, in this case probably does want to end up one stack frame back rather than several
    // instructions after the breakpoint due to the original "step into" command, so even when
    // "wrong" this current behavior isn't necessarily bad.
    controllers_.back()->Log("Continuing with this controller as primary.");
    ThreadController::ContinueOp op = controllers_.back()->GetContinueOp();
    if (op.synthetic_stop_) {
      // Synthetic stop. Skip notifying the backend and broadcast a stop notification for the
      // current state.
      controllers_.back()->Log("Synthetic stop.");
      debug_ipc::MessageLoop::Current()->PostTask(
          FROM_HERE, [thread = weak_factory_.GetWeakPtr()]() {
            if (thread) {
              StopInfo info;
              info.exception_type = debug_ipc::ExceptionType::kSynthetic;
              thread->OnException(info);
            }
          });
      return;
    } else {
      // Dispatch the continuation message.
      request.how = op.how;
      request.range_begin = op.range.begin();
      request.range_end = op.range.end();
    }
  }

  ClearFrames();
  session()->remote_api()->Resume(request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::ContinueWith(std::unique_ptr<ThreadController> controller,
                              fit::callback<void(const Err&)> on_continue) {
  ThreadController* controller_ptr = controller.get();

  // Add it first so that its presence will be noted by anything its initialization function does.
  controllers_.push_back(std::move(controller));

  controller_ptr->InitWithThread(
      this, [this, controller_ptr, on_continue = std::move(on_continue)](const Err& err) mutable {
        if (err.has_error()) {
          controller_ptr->Log("InitWithThread failed.");
          NotifyControllerDone(controller_ptr);  // Remove the controller.
        } else {
          controller_ptr->Log("Initialized, continuing...");
          Continue(false);
        }
        on_continue(err);
      });
}

void ThreadImpl::JumpTo(uint64_t new_address, fit::callback<void(const Err&)> cb) {
  // The register to set.
  debug_ipc::WriteRegistersRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  request.registers.emplace_back(
      GetSpecialRegisterID(session()->arch(), debug_ipc::SpecialRegisterType::kIP), new_address);

  // The "jump" command updates the thread's location so we need to recompute the stack. So once the
  // jump is complete we re-request the thread's status.
  //
  // This could be made faster by requesting status immediately after sending the update so we don't
  // have to wait for two round-trips, but that complicates the callback logic and this feature is
  // not performance- sensitive.
  //
  // Another approach is to make the register request message able to optionally request a stack
  // backtrace and include that in the reply.
  session()->remote_api()->WriteRegisters(
      request, [thread = weak_factory_.GetWeakPtr(), cb = std::move(cb)](
                   const Err& err, debug_ipc::WriteRegistersReply reply) mutable {
        if (err.has_error()) {
          cb(err);  // Transport error.
        } else if (reply.status != 0) {
          cb(Err("Could not set thread instruction pointer. Error %d (%s).", reply.status,
                 debug_ipc::ZxStatusToString(static_cast<uint32_t>(reply.status))));
        } else if (!thread) {
          cb(Err("Thread destroyed."));
        } else {
          // Success, update the current stack before issuing the callback.
          thread->SyncFramesForStack(std::move(cb));
        }
      });
}

void ThreadImpl::NotifyControllerDone(ThreadController* controller) {
  controller->Log("Controller done, removing.");

  // We expect to have few controllers so brute-force is sufficient.
  for (auto cur = controllers_.begin(); cur != controllers_.end(); ++cur) {
    if (cur->get() == controller) {
      controllers_.erase(cur);
      return;
    }
  }
  FX_NOTREACHED();  // Notification for unknown controller.
}

void ThreadImpl::StepInstruction() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);
  request.how = debug_ipc::ResumeRequest::How::kStepInstruction;
  session()->remote_api()->Resume(request, [](const Err& err, debug_ipc::ResumeReply) {});
}

const Stack& ThreadImpl::GetStack() const { return stack_; }

Stack& ThreadImpl::GetStack() { return stack_; }

void ThreadImpl::SetMetadata(const debug_ipc::ThreadRecord& record) {
  FX_DCHECK(koid_ == record.thread_koid);

  name_ = record.name;
  state_ = record.state;
  blocked_reason_ = record.blocked_reason;

  stack_.SetFrames(record.stack_amount, record.frames);
}

void ThreadImpl::OnException(const StopInfo& info) {
  if (settings().GetBool(ClientSettings::Thread::kDebugStepping)) {
    printf("----------\r\nGot %s exception @ 0x%" PRIx64 " in %s\r\n",
           debug_ipc::ExceptionTypeToString(info.exception_type), stack_[0]->GetAddress(),
           ThreadController::FrameFunctionNameForLog(stack_[0]).c_str());
  }

  if (stack_.empty()) {
    // Threads can stop with no stack if the thread is killed while processing an exception. If
    // this happens (or any other error that might cause an empty stack), declare all thread
    // controllers done since they can't meaningfully continue or process this state, and forcing
    // them all to separately check for an empty stack is error-prone.
    controllers_.clear();
  }

  // When any controller says "stop" it takes precendence and the thread will stop no matter what
  // any other controllers say.
  bool should_stop = false;

  // Set when any controller says "continue". If no controller says "stop" we need to differentiate
  // the case where there are no controllers or all controllers say "unexpected" (thread should
  // stop), from where one or more said "continue" (thread should continue, any "unexpected" votes
  // are ignored).
  bool have_continue = false;

  auto controller_iter = controllers_.begin();
  while (controller_iter != controllers_.end()) {
    ThreadController* controller = controller_iter->get();
    switch (controller->OnThreadStop(info.exception_type, info.hit_breakpoints)) {
      case ThreadController::kContinue:
        // Try the next controller.
        controller->Log("Reported continue on exception.");
        have_continue = true;
        controller_iter++;
        break;
      case ThreadController::kStopDone:
        // Once a controller tells us to stop, we assume the controller no longer applies and delete
        // it.
        //
        // Need to continue with checking all controllers even though we know we should stop at this
        // point. Multiple controllers should say "stop" at the same time and we need to be able to
        // delete all that no longer apply (say you did "finish", hit a breakpoint, and then
        // "finish" again, both finish commands would be active and you would want them both to be
        // completed when the current frame actually finishes).
        controller->Log("Reported stop on exception, stopping and removing it.");
        controller_iter = controllers_.erase(controller_iter);
        should_stop = true;
        break;
      case ThreadController::kUnexpected:
        // An unexpected exception means the controller is still active but doesn't know what to do
        // with this exception.
        controller->Log("Reported unexpected exception.");
        controller_iter++;
        break;
    }
  }

  if (!have_continue) {
    // No controller voted to continue (maybe all active controllers reported "unexpected") or there
    // was no controller. Such cases should stop.
    should_stop = true;
  }

  // The existence of any non-internal breakpoints being hit means the thread should always stop.
  // This check happens after notifying the controllers so if a controller triggers, it's counted as
  // a "hit" (otherwise, doing "run until" to a line with a normal breakpoint on it would keep the
  // "run until" operation active even after it was hit).
  //
  // Also, filter out internal breakpoints in the notification sent to the observers.
  StopInfo external_info = info;
  for (size_t i = 0; i < external_info.hit_breakpoints.size(); /* nothing */) {
    if (external_info.hit_breakpoints[i] && !external_info.hit_breakpoints[i]->IsInternal()) {
      should_stop = true;
      i++;
    } else {
      // Erase all deleted weak pointers and internal breakpoints.
      external_info.hit_breakpoints.erase(external_info.hit_breakpoints.begin() + i);
    }
  }

  // Non-debug exceptions also mean the thread should always stop (check this after running the
  // controllers for the same reason as the breakpoint check above).
  if (!debug_ipc::IsDebug(info.exception_type))
    should_stop = true;

  if (should_stop) {
    // Stay stopped and notify the observers.
    for (auto& observer : session()->thread_observers())
      observer.OnThreadStopped(this, external_info);
  } else {
    // Controllers all say to continue.
    Continue(false);
  }
}

void ThreadImpl::SyncFramesForStack(fit::callback<void(const Err&)> callback) {
  debug_ipc::ThreadStatusRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->remote_api()->ThreadStatus(
      request, [callback = std::move(callback), thread = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::ThreadStatusReply reply) mutable {
        if (err.has_error()) {
          callback(err);
          return;
        }

        if (!thread) {
          callback(Err("Thread destroyed."));
          return;
        }

        thread->SetMetadata(reply.record);
        callback(Err());
      });
}

std::unique_ptr<Frame> ThreadImpl::MakeFrameForStack(const debug_ipc::StackFrame& input,
                                                     Location location) {
  return std::make_unique<FrameImpl>(this, input, std::move(location));
}

Location ThreadImpl::GetSymbolizedLocationForStackFrame(const debug_ipc::StackFrame& input) {
  auto vect = GetProcess()->GetSymbols()->ResolveInputLocation(InputLocation(input.ip));

  // Symbolizing an address should always give exactly one result.
  FX_DCHECK(vect.size() == 1u);
  return vect[0];
}

void ThreadImpl::ClearFrames() {
  if (stack_.ClearFrames()) {
    for (auto& observer : session()->thread_observers())
      observer.OnThreadFramesInvalidated(this);
  }
}

}  // namespace zxdb
