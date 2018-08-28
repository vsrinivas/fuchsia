// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include <iostream>
#include <limits>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
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
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::ContinueWith(std::unique_ptr<ThreadController> controller) {
  controllers_.push_back(std::move(controller));
  Continue();
}

void ThreadImpl::NotifyControllerDone(ThreadController* controller) {
  // We expect to have few controllers so brute-force is sufficient.
  for (auto cur = controllers_.begin(); cur != controllers_.end(); ++cur) {
    if (cur->get() == controller) {
      controllers_.erase(cur);
      return;
    }
  }
  FXL_NOTREACHED();  // Notification for unknown controller.
}

Err ThreadImpl::Step() {
  if (frames_.empty())
    return Err("Thread has no current address to step.");

  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);
  request.how = debug_ipc::ResumeRequest::How::kStepInRange;

  LineDetails line_details =
      process_->GetSymbols()->LineDetailsForAddress(frames_[0]->GetAddress());
  if (line_details.entries().empty()) {
    // When there are no symbols, fall back to step instruction.
    StepInstruction();
    return Err();
  }

  request.range_begin = line_details.entries()[0].range.begin();
  request.range_end = line_details.entries().back().range.end();

  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
  return Err();
}

void ThreadImpl::StepInstruction() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koids.push_back(koid_);
  request.how = debug_ipc::ResumeRequest::How::kStepInstruction;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::Finish(const Frame* frame,
                        std::function<void(const Err&)> cb) {
  cb(Err("'Finish' is temporarily closed for construction. "
         "Please try again in a few days."));
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
      request, [callback, thread = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::BacktraceReply reply) {
        if (!thread)
          return;
        thread->SaveFrames(reply.frames, true);
        if (callback)
          callback();
      });
}

void ThreadImpl::GetRegisters(
    std::function<void(const Err&, const RegisterSet&)> callback) {
  debug_ipc::RegistersRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->remote_api()->Registers(
      request, [thread = weak_factory_.GetWeakPtr(), callback](
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

  std::vector<debug_ipc::StackFrame> frames;
  frames.push_back(notify.frame);
  SaveFrames(frames, false);
}

void ThreadImpl::OnException(
    debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  bool should_stop;
  if (controllers_.empty()) {
    // When there are no controllers, all stops are effective. And any
    // breakpoints being hit take precedence over any stepping being done by
    // controllers.
    should_stop = true;
  } else {
    // Ask all controllers, topmost first. If a controller says "continue", it
    // means that controller doesn't think this stop applies to it and we
    // should ask the next controller. We actually continue only if all
    // controllers agree the thread should continue.
    should_stop = false;
    // Don't use iterators since the map is mutated in the loop.
    for (int i = static_cast<int>(controllers_.size()) - 1;
         !should_stop && i >= 0; i--) {
      switch (controllers_[i]->OnThreadStop(type, hit_breakpoints)) {
        case ThreadController::kContinue:
          // Try the next controller.
          continue;
        case ThreadController::kStop:
          // Once a controller tells us to stop, we assume the controller no
          // longer applies and delete it.
          controllers_.erase(controllers_.begin() + i);
          should_stop = true;
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
