// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

ThreadImpl::ThreadImpl(ProcessImpl* process,
                       const debug_ipc::ThreadRecord& record)
    : Thread(process->session()), process_(process), koid_(record.koid),
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
  session()->Send<debug_ipc::PauseRequest, debug_ipc::PauseReply>(
      request,
      [](const Err& err, debug_ipc::PauseReply) {});
}

void ThreadImpl::Continue() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->Send<debug_ipc::ResumeRequest, debug_ipc::ResumeReply>(
      request,
      [](const Err& err, debug_ipc::ResumeReply) {});
}

void ThreadImpl::StepInstruction() {
  debug_ipc::ResumeRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;
  request.how = debug_ipc::ResumeRequest::How::kStepInstruction;
  session()->Send<debug_ipc::ResumeRequest, debug_ipc::ResumeReply>(
      request,
      [](const Err& err, debug_ipc::ResumeReply) {});
}

const std::vector<std::unique_ptr<Frame>>& ThreadImpl::GetFrames() const {
  return frames_;
}

bool ThreadImpl::HasAllFrames() const {
  return has_all_frames_;
}

void ThreadImpl::SyncFrames(std::function<void()> callback) {
  debug_ipc::BacktraceRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->Send<debug_ipc::BacktraceRequest, debug_ipc::BacktraceReply>(
      request,
      [callback, thread = weak_factory_.GetWeakPtr()](
          const Err& err, debug_ipc::BacktraceReply reply) {
        if (thread)
          thread->HaveFrames(reply.frames);
        if (callback)
          callback();
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

void ThreadImpl::OnException(const debug_ipc::NotifyException& notify) {
  SetMetadata(notify.thread);

  // After an exception the thread should be blocked.
  FXL_DCHECK(state_ == debug_ipc::ThreadRecord::State::kBlocked);

  // May have had some out-of-date backtrace information if the thread was
  // resumed without this class noticing.
  if (!frames_.empty())
    ClearFrames();

  // Save the current position as frame 0.
  has_all_frames_ = false;
  frames_.push_back(std::make_unique<FrameImpl>(this, notify.frame));

  for (auto& observer : observers())
    observer.OnThreadStopped(this, notify.type);
}

void ThreadImpl::HaveFrames(const std::vector<debug_ipc::StackFrame>& frames) {
  // TODO(brettw) need to preserve stack frames that haven't changed.
  frames_.clear();
  for (const auto& frame : frames)
    frames_.push_back(std::make_unique<FrameImpl>(this, frame));
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
