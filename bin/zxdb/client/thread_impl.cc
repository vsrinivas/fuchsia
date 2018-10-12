// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include <inttypes.h>

#include <iostream>
#include <limits>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

ThreadImpl::ThreadImpl(ProcessImpl* process,
                       const debug_ipc::ThreadRecord& record)
    : Thread(process->session()),
      process_(process),
      koid_(record.koid),
      weak_factory_(this) {
  SetMetadata(record);
  settings_.set_fallback(&process->settings());
}

ThreadImpl::~ThreadImpl() = default;

Process* ThreadImpl::GetProcess() const { return process_; }

uint64_t ThreadImpl::GetKoid() const { return koid_; }

const std::string& ThreadImpl::GetName() const { return name_; }

debug_ipc::ThreadRecord::State ThreadImpl::GetState() const { return state_; }

void ThreadImpl::Pause() {
  debug_ipc::PauseRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  session()->remote_api()->Pause(request,
                                 [](const Err& err, debug_ipc::PauseReply) {});
}

void ThreadImpl::Continue() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);

  if (controllers_.empty()) {
    request.how = debug_ipc::ResumeRequest::How::kContinue;
  } else {
    // When there are thread controllers, ask the most recent one for how to
    // continue.
    //
    // Theoretically we're running with all controllers at once and we want to
    // stop at the first one that triggers, which means we want to compute the
    // most restrictive intersection of all of them.
    //
    // This is annoying to implement and it's difficult to construct a
    // situation where this would be required. The controller that doesn't
    // involve breakpoints is "step in range" and generally ranges refer to
    // code lines that will align. Things like "until" are implemented with
    // breakpoints so can overlap arbitrarily with other operations with no
    // problem.
    //
    // A case where this might show up:
    //  1. Do "step into" which steps through a range of instructions.
    //  2. In the middle of that range is a breakpoint that's hit.
    //  3. The user does "finish." We'll ask the finish controller what to do
    //     and it will say "continue" and the range from step 1 is lost.
    // However, in this case probably does want to end up one stack frame
    // back rather than several instructions after the breakpoint due to the
    // original "step into" command, so even when "wrong" this current behavior
    // isn't necessarily bad.
    controllers_.back()->Log("Continuing with this controller as primary.");
    ThreadController::ContinueOp op = controllers_.back()->GetContinueOp();
    request.how = op.how;
    request.range_begin = op.range.begin();
    request.range_end = op.range.end();
  }

  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::ContinueWith(std::unique_ptr<ThreadController> controller,
                              std::function<void(const Err&)> on_continue) {
  ThreadController* controller_ptr = controller.get();

  // Add it first so that its presence will be noted by anything its
  // initialization function does.
  controllers_.push_back(std::move(controller));

  controller_ptr->InitWithThread(
      this, [ this, controller_ptr,
              on_continue = std::move(on_continue) ](const Err& err) {
        if (err.has_error()) {
          controller_ptr->Log("InitWithThread failed.");
          NotifyControllerDone(controller_ptr);  // Remove the controller.
        } else {
          controller_ptr->Log("Initialized, continuing...");
          Continue();
        }
        on_continue(err);
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
  FXL_NOTREACHED();  // Notification for unknown controller.
}

void ThreadImpl::StepInstruction() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);
  request.how = debug_ipc::ResumeRequest::How::kStepInstruction;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

std::vector<Frame*> ThreadImpl::GetFrames() const {
  std::vector<Frame*> frames;
  frames.reserve(frames_.size());
  for (const auto& cur : frames_)
    frames.push_back(cur.get());
  return frames;
}

bool ThreadImpl::HasAllFrames() const { return has_all_frames_; }

void ThreadImpl::SyncFrames(std::function<void()> callback) {
  debug_ipc::BacktraceRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->remote_api()->Backtrace(
      request, [ callback, thread = weak_factory_.GetWeakPtr() ](
                   const Err& err, debug_ipc::BacktraceReply reply) {
        if (!thread)
          return;
        thread->SaveFrames(reply.frames, true);
        if (callback)
          callback();
      });
}

FrameFingerprint ThreadImpl::GetFrameFingerprint(size_t frame_index) const {
  // See function comment in thread.h for more. We need to look at the next
  // frame, so either we need to know we got them all or the caller wants the
  // 0th one. We should always have the top two stack entries if available,
  // so having only one means we got them all.
  FXL_DCHECK(frame_index == 0 || HasAllFrames());

  // Should reference a valid index in the array.
  if (frame_index >= frames_.size()) {
    FXL_NOTREACHED();
    return FrameFingerprint();
  }

  // The frame address requires looking at the previour frame. When this is the
  // last entry, we can't do that. This returns the frame base pointer instead
  // which will at least identify the frame in some ways, and can be used to
  // see if future frames are younger.
  size_t prev_frame_index = frame_index + 1;
  if (prev_frame_index == frames_.size())
    return FrameFingerprint(frames_[frame_index]->GetStackPointer());

  // Use the previuos frame's stack pointer. See frame_fingerprint.h.
  return FrameFingerprint(frames_[prev_frame_index]->GetStackPointer());
}

void ThreadImpl::GetRegisters(
    std::vector<debug_ipc::RegisterCategory::Type> cats_to_get,
    std::function<void(const Err&, const RegisterSet&)> callback) {
  debug_ipc::RegistersRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  request.categories = std::move(cats_to_get);

  session()->remote_api()->Registers(
      request, [ thread = weak_factory_.GetWeakPtr(), callback ](
                   const Err& err, debug_ipc::RegistersReply reply) {
        thread->registers_ = std::make_unique<RegisterSet>(
            thread->session()->arch(), std::move(reply.categories));
        if (callback)
          callback(err, *thread->registers_.get());
      });
}

void ThreadImpl::SetMetadata(const debug_ipc::ThreadRecord& record) {
  FXL_DCHECK(koid_ == record.koid);

  // Any stack frames need clearing when we transition to running. Do the
  // notification after updating the state so code handling the notification
  // will see the latest values.
  bool frames_need_clearing =
      state_ != debug_ipc::ThreadRecord::State::kRunning &&
      record.state == debug_ipc::ThreadRecord::State::kRunning;

  name_ = record.name;
  state_ = record.state;

  if (frames_need_clearing)
    ClearFrames();
}

void ThreadImpl::SetMetadataFromException(
    const debug_ipc::NotifyException& notify) {
  SetMetadata(notify.thread);

  // After an exception the thread should be blocked.
  FXL_DCHECK(state_ == debug_ipc::ThreadRecord::State::kBlocked);

  FXL_DCHECK(!notify.frames.empty());
  SaveFrames(notify.frames, false);
}

void ThreadImpl::OnException(
    debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
#if defined(DEBUG_THREAD_CONTROLLERS)
  ThreadController::LogRaw("----------\r\nGot exception @ 0x%" PRIx64,
  frames_[0]->GetAddress());
#endif
  bool should_stop;
  if (controllers_.empty()) {
    // When there are no controllers, all stops are effective.
    should_stop = true;
  } else {
    // Ask all controllers and continue only if all controllers agree the
    // thread should continue. Multiple controllers should say "stop" at the
    // same time and we need to be able to delete all that no longer apply
    // (say you did "finish", hit a breakpoint, and then "finish" again, both
    // finish commands would be active and you would want them both to be
    // completed when the current frame actually finishes).
    should_stop = false;
    // Don't use iterators since the map is mutated in the loop.
    for (int i = 0; i < static_cast<int>(controllers_.size()); i++) {
      switch (controllers_[i]->OnThreadStop(type, hit_breakpoints)) {
        case ThreadController::kContinue:
          // Try the next controller.
          controllers_[i]->Log("Reported continue on exception.");
          continue;
        case ThreadController::kStop:
          // Once a controller tells us to stop, we assume the controller no
          // longer applies and delete it.
          controllers_[i]->Log(
              "Reported stop on exception, stopping and removing it.");
          controllers_.erase(controllers_.begin() + i);
          should_stop = true;
          i--;
          break;
      }
    }
  }

  // The existance of any non-internal breakpoints being hit means the thread
  // should always stop. This check happens after notifying the controllers so
  // if a controller triggers, it's counted as a "hit" (otherwise, doing
  // "run until" to a line with a normal breakpoint on it would keep the "run
  // until" operation active even after it was hit).
  //
  // Also, filter out internal breakpoints in the notification sent to the
  // observers.
  std::vector<fxl::WeakPtr<Breakpoint>> external_breakpoints;
  for (auto& hit : hit_breakpoints) {
    if (!hit)
      continue;

    if (!hit->IsInternal()) {
      external_breakpoints.push_back(hit);
      should_stop = true;
      break;
    }
  }

  // Non-debug exceptions also mean the thread should always stop (check this
  // after running the controllers for the same reason as the breakpoint check
  // above).
  if (type == debug_ipc::NotifyException::Type::kGeneral)
    should_stop = true;

  if (should_stop) {
    // Stay stopped and notify the observers.
    for (auto& observer : observers())
      observer.OnThreadStopped(this, type, external_breakpoints);
  } else {
    // Controllers all say to continue.
    Continue();
  }
}

void ThreadImpl::SaveFrames(const std::vector<debug_ipc::StackFrame>& frames,
                            bool have_all) {
  // The goal is to preserve pointer identity for frames. If a frame is the
  // same, weak pointers to it should remain valid.
  using IpSp = std::pair<uint64_t, uint64_t>;
  std::map<IpSp, std::unique_ptr<FrameImpl>> existing;
  for (auto& cur : frames_) {
    IpSp key(cur->GetAddress(), cur->GetStackPointer());
    existing[key] = std::move(cur);
  }

  frames_.clear();
  for (size_t i = 0; i < frames.size(); i++) {
    IpSp key(frames[i].ip, frames[i].sp);
    auto found = existing.find(key);
    if (found == existing.end()) {
      // New frame we haven't seen.
      frames_.push_back(std::make_unique<FrameImpl>(
          this, frames[i], Location(Location::State::kAddress, frames[i].ip)));
    } else {
      // Can re-use existing pointer.
      frames_.push_back(std::move(found->second));
      existing.erase(found);
    }
  }

  has_all_frames_ = have_all;
}

void ThreadImpl::ClearFrames() {
  has_all_frames_ = false;

  if (frames_.empty())
    return;  // Nothing to do.

  frames_.clear();
  for (auto& observer : observers())
    observer.OnThreadFramesInvalidated(this);
}

}  // namespace zxdb
