// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include <inttypes.h>  // ERSASEMME(brettw)
#include <iostream>

#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
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

  ClearFrames();

  session()->remote_api()->Backtrace(
      request, [callback, thread = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::BacktraceReply reply) {
        if (!thread)
          return;

        thread->HaveFrames(reply.frames, [thread, callback]() {
          // HaveFrames will only issue the callback if the ThreadImpl is
          // still in scope so we don't need a weak pointer here.
          thread->has_all_frames_ = true;
          if (callback)
            callback();
        });
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
  // Symbolize the stack frame before broadcasting the state change.
  std::vector<debug_ipc::StackFrame> frame;
  frame.push_back(notify.frame);
  // HaveFrames will only issue the callback if the ThreadImpl is still in
  // scope so we don't need a weak pointer here.
  HaveFrames(frame, [notify, thread = this]() {
    thread->SetMetadata(notify.thread);

    // After an exception the thread should be blocked.
    FXL_DCHECK(thread->state_ == debug_ipc::ThreadRecord::State::kBlocked);

    thread->has_all_frames_ = false;
    ;
    for (auto& observer : thread->observers())
      observer.OnThreadStopped(thread, notify.type);
  });
}

void ThreadImpl::HaveFrames(const std::vector<debug_ipc::StackFrame>& frames,
                            std::function<void()> callback) {
  // TODO(brettw) need to preserve stack frames that haven't changed.
  std::vector<uint64_t> addresses;
  for (const auto& frame : frames)
    addresses.push_back(frame.ip);

  frames_.clear();
  for (size_t i = 0; i < frames.size(); i++) {
    frames_.push_back(std::make_unique<FrameImpl>(
        this, frames[i], Location(Location::State::kAddress, frames[i].ip)));
  }

  if (callback)
    callback();
}

void ThreadImpl::ClearFrames() {
  has_all_frames_ = false;

  if (frames_.empty())
    return;  // Nothing to do.

  frames_.clear();
  for (auto& observer : observers())
    observer.OnThreadFramesInvalidated(this);
}

void ThreadImpl::GetRegisters(
    std::function<void(const Err&, std::vector<debug_ipc::Register>)>
        callback) {
  debug_ipc::RegistersRequest request;
  request.process_koid = process_->GetKoid();
  request.thread_koid = koid_;

  session()->remote_api()->Registers(
      request, [process = weak_factory_.GetWeakPtr(), callback](
                   const Err& err, debug_ipc::RegistersReply reply) {
        if (callback)
          callback(err, std::move(reply.registers));
      });
}

}  // namespace zxdb
