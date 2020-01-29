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

void MockRemoteAPI::AddMemory(uint64_t address, std::vector<uint8_t> data) {
  memory_.AddMemory(address, std::move(data));
}

void MockRemoteAPI::SetRegisterCategory(debug_ipc::RegisterCategory cat,
                                        std::vector<debug_ipc::Register> regs) {
  register_replies_[cat] = std::move(regs);
}

void MockRemoteAPI::Attach(const debug_ipc::AttachRequest& request,
                           fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) {
  debug_ipc::AttachReply reply;
  reply.koid = request.koid;
  reply.name = "<mock>";

  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb), reply]() mutable { cb(Err(), reply); });
}

void MockRemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  breakpoint_add_count_++;
  last_breakpoint_add_ = request;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
    cb(Err(), debug_ipc::AddOrChangeBreakpointReply());
  });
}

void MockRemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  breakpoint_remove_count_++;
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
}

void MockRemoteAPI::ThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                                 fit::callback<void(const Err&, debug_ipc::ThreadStatusReply)> cb) {
  // Returns the canned response.
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb), response = thread_status_reply_]() mutable {
        cb(Err(), std::move(response));
      });
}

void MockRemoteAPI::Resume(const debug_ipc::ResumeRequest& request,
                           fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) {
  // Always returns success.
  resume_count_++;
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb), resume_quits_loop = resume_quits_loop_]() mutable {
        cb(Err(), debug_ipc::ResumeReply());
        if (resume_quits_loop)
          debug_ipc::MessageLoop::Current()->QuitNow();
      });
}

void MockRemoteAPI::ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                               fit::callback<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  std::vector<uint8_t> result = memory_.ReadMemory(request.address, request.size);
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                              [request, result, cb = std::move(cb)]() mutable {
                                                debug_ipc::ReadMemoryReply reply;

                                                // For now this is very simple and returns the
                                                // result as one block. A more complete
                                                // implementation would convert short reads into
                                                // multiple blocks.
                                                reply.blocks.resize(1);
                                                auto& block = reply.blocks[0];

                                                block.address = request.address;
                                                block.valid = request.size > 0;
                                                block.size = request.size;
                                                if (block.valid)
                                                  block.data = std::move(result);

                                                cb(Err(), std::move(reply));
                                              });
}

void MockRemoteAPI::ReadRegisters(
    const debug_ipc::ReadRegistersRequest& request,
    fit::callback<void(const Err&, debug_ipc::ReadRegistersReply)> cb) {
  debug_ipc::ReadRegistersReply reply;
  for (auto& cat : request.categories) {
    const auto& regs = register_replies_[cat];
    reply.registers.insert(reply.registers.end(), regs.begin(), regs.end());
  }

  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [reply, cb = std::move(cb)]() mutable { cb(Err(), reply); });
}

void MockRemoteAPI::WriteRegisters(
    const debug_ipc::WriteRegistersRequest& request,
    fit::callback<void(const Err&, debug_ipc::WriteRegistersReply)> cb) {
  last_write_registers_ = request;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
    debug_ipc::WriteRegistersReply reply;
    reply.status = 0;
    cb(Err(), reply);
  });
}

}  // namespace zxdb
