// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/remote_api_impl.h"

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
//#include "garnet/lib/debug_ipc/helper/buffered_fd.h"

namespace zxdb {

RemoteAPIImpl::RemoteAPIImpl(Session* session) : session_(session) {}
RemoteAPIImpl::~RemoteAPIImpl() = default;

void RemoteAPIImpl::Hello(
    const debug_ipc::HelloRequest& request,
    std::function<void(const Err&, debug_ipc::HelloReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Launch(
    const debug_ipc::LaunchRequest& request,
    std::function<void(const Err&, debug_ipc::LaunchReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Kill(
    const debug_ipc::KillRequest& request,
    std::function<void(const Err&, debug_ipc::KillReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Attach(
    const debug_ipc::AttachRequest& request,
    std::function<void(const Err&, debug_ipc::AttachReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Detach(
    const debug_ipc::DetachRequest& request,
    std::function<void(const Err&, debug_ipc::DetachReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Modules(
    const debug_ipc::ModulesRequest& request,
    std::function<void(const Err&, debug_ipc::ModulesReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Pause(
    const debug_ipc::PauseRequest& request,
    std::function<void(const Err&, debug_ipc::PauseReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Resume(
    const debug_ipc::ResumeRequest& request,
    std::function<void(const Err&, debug_ipc::ResumeReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::ProcessTree(
    const debug_ipc::ProcessTreeRequest& request,
    std::function<void(const Err&, debug_ipc::ProcessTreeReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Threads(
    const debug_ipc::ThreadsRequest& request,
    std::function<void(const Err&, debug_ipc::ThreadsReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::ReadMemory(
    const debug_ipc::ReadMemoryRequest& request,
    std::function<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Registers(
    const debug_ipc::RegistersRequest& request,
    std::function<void(const Err&, debug_ipc::RegistersReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::Backtrace(
    const debug_ipc::BacktraceRequest& request,
    std::function<void(const Err&, debug_ipc::BacktraceReply)> cb) {
  Send(request, std::move(cb));
}

void RemoteAPIImpl::AddressSpace(
    const debug_ipc::AddressSpaceRequest& request,
    std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb) {
  Send(request, std::move(cb));
}

template <typename SendMsgType, typename RecvMsgType>
void RemoteAPIImpl::Send(
    const SendMsgType& send_msg,
    std::function<void(const Err&, RecvMsgType)> callback) {
  uint32_t transaction_id = session_->next_transaction_id_;
  session_->next_transaction_id_++;

  if (!session_->stream_) {
    // No connection, asynchronously issue the error.
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask([callback]() {
        callback(
            Err(ErrType::kNoConnection, "No connection to debugged system."),
            RecvMsgType());
      });
    }
    return;
  }

  debug_ipc::MessageWriter writer(sizeof(SendMsgType));
  debug_ipc::WriteRequest(send_msg, transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();
  session_->stream_->Write(std::move(serialized));

  // This is the reply callback that unpacks the data in a vector, converts it
  // to the requested RecvMsgType struct, and issues the callback.
  Session::Callback dispatch_callback =
      [callback = std::move(callback)](const Err& err, std::vector<char> data) {
        RecvMsgType reply;
        if (err.has_error()) {
          // Forward the error and ignore all data.
          if (callback)
            callback(err, std::move(reply));
          return;
        }

        debug_ipc::MessageReader reader(std::move(data));

        uint32_t transaction_id = 0;
        Err deserialization_err;
        if (!debug_ipc::ReadReply(&reader, &reply, &transaction_id)) {
          reply = RecvMsgType();  // Could be in a half-read state.
          deserialization_err =
              Err(ErrType::kCorruptMessage,
                  fxl::StringPrintf("Corrupt reply message for transaction %u.",
                                    transaction_id));
        }

        if (callback)
          callback(deserialization_err, std::move(reply));
      };

  session_->pending_.emplace(
      std::piecewise_construct, std::forward_as_tuple(transaction_id),
      std::forward_as_tuple(std::move(dispatch_callback)));
}

}  // namespace zxdb
