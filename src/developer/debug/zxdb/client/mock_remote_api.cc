// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_remote_api.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

MockRemoteAPI::MockRemoteAPI() = default;
MockRemoteAPI::~MockRemoteAPI() = default;

int MockRemoteAPI::GetAndResetResumeCount() {
  int result = resume_count_;
  resume_count_ = 0;
  return result;
}

void MockRemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  breakpoint_add_count_++;
  last_breakpoint_add_ = request;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb]() {
    cb(Err(), debug_ipc::AddOrChangeBreakpointReply());
  });
}

void MockRemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  breakpoint_remove_count_++;
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
}

void MockRemoteAPI::ThreadStatus(
    const debug_ipc::ThreadStatusRequest& request,
    std::function<void(const Err&, debug_ipc::ThreadStatusReply)> cb) {
  // Returns the canned response.
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb, response = thread_status_reply_]() {
        cb(Err(), std::move(response));
      });
}

void MockRemoteAPI::Resume(
    const debug_ipc::ResumeRequest& request,
    std::function<void(const Err&, debug_ipc::ResumeReply)> cb) {
  // Always returns success.
  resume_count_++;
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb, resume_quits_loop = resume_quits_loop_]() {
        cb(Err(), debug_ipc::ResumeReply());
        if (resume_quits_loop)
          debug_ipc::MessageLoop::Current()->QuitNow();
      });
}

void MockRemoteAPI::WriteRegisters(
    const debug_ipc::WriteRegistersRequest& request,
    std::function<void(const Err&, debug_ipc::WriteRegistersReply)> cb) {
  last_write_registers_ = request;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb]() {
    debug_ipc::WriteRegistersReply reply;
    reply.status = 0;
    cb(Err(), reply);
  });
}

}  // namespace zxdb
