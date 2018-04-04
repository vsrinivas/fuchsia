// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

ThreadImpl::ThreadImpl(ProcessImpl* process,
                       const debug_ipc::ThreadRecord& record)
    : Thread(process->session()), process_(process), koid_(record.koid) {
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

void ThreadImpl::SetMetadata(const debug_ipc::ThreadRecord& record) {
  FXL_DCHECK(koid_ == record.koid);
  name_ = record.name;
  state_ = record.state;
}

void ThreadImpl::OnException(const debug_ipc::NotifyException& notify) {
  SetMetadata(notify.thread);

  // After an exception the thread should be blocked.
  FXL_DCHECK(state_ == debug_ipc::ThreadRecord::State::kBlocked);

  for (auto& observer : observers())
    observer.OnThreadStopped(this, notify.type, notify.frame.ip);
}

}  // namespace zxdb
