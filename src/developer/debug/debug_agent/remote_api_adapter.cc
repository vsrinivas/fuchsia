// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/remote_api_adapter.h"

#include "src/developer/debug/debug_agent/remote_api.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/stream_buffer.h"

namespace debug_agent {

namespace {

// Deserializes the request based on type, calls the given hander in the
// RemoteAPI, and then sends the reply back to the client.
template <typename RequestMsg, typename ReplyMsg>
void DispatchMessage(RemoteAPIAdapter* adapter,
                     void (RemoteAPI::*handler)(const RequestMsg&, ReplyMsg*),
                     std::vector<char> data, const char* type_string) {
  RequestMsg request;

  uint32_t transaction_id = 0;
  if (!debug_ipc::Deserialize(std::move(data), &request, &transaction_id)) {
    LOGS(Error) << "Got bad debugger " << type_string << "Request, ignoring.";
    return;
  }

  ReplyMsg reply;
  (adapter->api()->*handler)(request, &reply);

  adapter->stream()->Write(debug_ipc::Serialize(reply, transaction_id));
}

}  // namespace

RemoteAPIAdapter::RemoteAPIAdapter(RemoteAPI* remote_api, debug::StreamBuffer* stream)
    : api_(remote_api), stream_(stream) {}

RemoteAPIAdapter::~RemoteAPIAdapter() {}

void RemoteAPIAdapter::OnStreamReadable() {
  while (true) {
    debug_ipc::MsgHeader header;
    size_t bytes_read = stream_->Peek(reinterpret_cast<char*>(&header), sizeof(header));
    if (bytes_read != sizeof(header))
      return;  // Don't have enough data for the header.
    if (!stream_->IsAvailable(header.size))
      return;  // Entire message hasn't arrived yet.

    // The message size includes the header.
    std::vector<char> buffer(header.size);
    stream_->Read(buffer.data(), header.size);

    // Range check the message type.
    if (header.type == debug_ipc::MsgHeader::Type::kNone ||
        header.type >= debug_ipc::MsgHeader::Type::kNumMessages) {
      LOGS(Error) << "Invalid message type " << static_cast<uint32_t>(header.type) << ", ignoring.";
      return;
    }

// Dispatches a message type assuming the handler function name, request
// struct type, and reply struct type are all based on the message type name.
// For example, MsgHeader::Type::kFoo will call:
//   api->OnFoo(FooRequest, FooReply*);
#define DISPATCH(msg_type)                                                     \
  case debug_ipc::MsgHeader::Type::k##msg_type:                                \
    DispatchMessage<debug_ipc::msg_type##Request, debug_ipc::msg_type##Reply>( \
        this, &RemoteAPI::On##msg_type, std::move(buffer), #msg_type);         \
    break

    switch (header.type) {
      DISPATCH(AddOrChangeBreakpoint);
      DISPATCH(AddressSpace);
      DISPATCH(Detach);
      DISPATCH(UpdateFilter);
      DISPATCH(Hello);
      DISPATCH(Kill);
      DISPATCH(Launch);
      DISPATCH(Modules);
      DISPATCH(Pause);
      DISPATCH(ProcessTree);
      DISPATCH(ReadMemory);
      DISPATCH(ReadRegisters);
      DISPATCH(WriteRegisters);
      DISPATCH(RemoveBreakpoint);
      DISPATCH(Resume);
      DISPATCH(Status);
      DISPATCH(SysInfo);
      DISPATCH(ThreadStatus);
      DISPATCH(Threads);
      DISPATCH(WriteMemory);
      DISPATCH(LoadInfoHandleTable);
      DISPATCH(UpdateGlobalSettings);
      DISPATCH(SaveMinidump);

      // Attach is special (see remote_api.h): forward the raw data instead of
      // a deserialized version.
      case debug_ipc::MsgHeader::Type::kAttach:
        api_->OnAttach(std::move(buffer));
        break;

      // Explicitly no "default" to get warnings about unhandled message types,
      // but need to handle these "not a message" types to avoid this warning.
      case debug_ipc::MsgHeader::Type::kNone:
      case debug_ipc::MsgHeader::Type::kNumMessages:
      case debug_ipc::MsgHeader::Type::kNotifyException:
      case debug_ipc::MsgHeader::Type::kNotifyIO:
      case debug_ipc::MsgHeader::Type::kNotifyModules:
      case debug_ipc::MsgHeader::Type::kNotifyProcessExiting:
      case debug_ipc::MsgHeader::Type::kNotifyProcessStarting:
      case debug_ipc::MsgHeader::Type::kNotifyThreadStarting:
      case debug_ipc::MsgHeader::Type::kNotifyThreadExiting:
      case debug_ipc::MsgHeader::Type::kNotifyLog:
      case debug_ipc::MsgHeader::Type::kNotifyComponentExiting:
      case debug_ipc::MsgHeader::Type::kNotifyComponentStarting:
        break;  // Avoid warning
    }

#undef DISPATCH
  }
}

}  // namespace debug_agent
