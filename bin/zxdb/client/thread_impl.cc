// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include <iostream>
#include <limits>

#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/run_until.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
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
  request.thread_koid = koid_;
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::ContinueUntil(const InputLocation& location,
                               std::function<void(const Err&)> cb) {
  RunUntil(this, location, std::move(cb));
}

Err ThreadImpl::Step() {
  if (frames_.empty())
    return Err("Thread has no current address to step.");

  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
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
  request.thread_koid = koid_;
  request.how = debug_ipc::ResumeRequest::How::kStepInstruction;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::Finish(const Frame* frame,
                        std::function<void(const Err&)> cb) {
  // This stores the frame as IP/SP rather than as a weak frame pointer. If the
  // thread stops in between the time this was issued and the time the callback
  // runs, lower frames will be cleared even if they're still valid. Therefore,
  // the only way we can re-match the stack frame is by IP/SP.
  auto on_have_frames = [
    weak_thread = weak_factory_.GetWeakPtr(), cb = std::move(cb),
    ip = frame->GetAddress(), sp = frame->GetStackPointer()
  ]() {
    if (weak_thread) {
      weak_thread->FinishWithFrames(ip, sp, std::move(cb));
    } else {
      cb(Err("The tread destroyed before \"Finish\" could be executed."));
    }
  };

  if (!HasAllFrames())
    SyncFrames(on_have_frames);
  else
    on_have_frames();
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

void ThreadImpl::GetRegisters(
    std::function<void(const Err&, std::vector<debug_ipc::Register>)>
        callback) {
  debug_ipc::RegistersRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->remote_api()->Registers(
      request, [ process = weak_factory_.GetWeakPtr(), callback ](
                   const Err& err, debug_ipc::RegistersReply reply) {
        if (callback)
          callback(err, std::move(reply.registers));
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

void ThreadImpl::DispatchExceptionNotification(
    debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  for (auto& observer : observers())
    observer.OnThreadStopped(this, type, hit_breakpoints);
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

void ThreadImpl::FinishWithFrames(uint64_t frame_ip, uint64_t frame_sp,
                                  std::function<void(const Err&)> cb) {
  // Find the frame corresponding to the reqested one.
  constexpr size_t kNotFound = std::numeric_limits<size_t>::max();
  size_t requested_index = kNotFound;
  for (size_t i = 0; i < frames_.size(); i++) {
    if (frames_[i]->GetAddress() == frame_ip &&
        frames_[i]->GetStackPointer() == frame_sp) {
      requested_index = i;
      break;
    }
  }
  if (requested_index == kNotFound) {
    cb(Err("The stack frame was destroyed before \"finish\" could run."));
    return;
  }

  if (requested_index == frames_.size() - 1) {
    // "Finish" from the bottom-most stack frame just continues the
    // program to completion.
    Continue();
    cb(Err());
    return;
  }

  // The stack frame to exit to is just the next one up. Be careful to avoid
  // forcing symbolizing when getting the frame's address.
  Frame* step_to = frames_[requested_index + 1].get();
  RunUntil(this, InputLocation(step_to->GetAddress()), frame_sp, std::move(cb));
}

}  // namespace zxdb
