// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/remote_api_adapter.h"

#include "garnet/bin/debug_agent/remote_api.h"
#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_agent {

namespace {

// Deserializes the request based on type, calls the given hander in the
// RemoteAPI, and then sends the reply back to the client.
template <typename RequestMsg, typename ReplyMsg>
void DispatchMessage(RemoteAPIAdapter* adapter,
                     void (RemoteAPI::*handler)(const RequestMsg&, ReplyMsg*),
                     std::vector<char> data, const char* type_string) {
  debug_ipc::MessageReader reader(std::move(data));

  RequestMsg request;
  uint32_t transaction_id = 0;
  if (!debug_ipc::ReadRequest(&reader, &request, &transaction_id)) {
    fprintf(stderr, "Got bad debugger %sRequest, ignoring.\n", type_string);
    return;
  }

  ReplyMsg reply;
  (adapter->api()->*handler)(request, &reply);

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);

  adapter->stream()->Write(writer.MessageComplete());
}

}  // namespace

RemoteAPIAdapter::RemoteAPIAdapter(RemoteAPI* remote_api,
                                   debug_ipc::StreamBuffer* stream)
    : api_(remote_api), stream_(stream) {}

RemoteAPIAdapter::~RemoteAPIAdapter() {}

void RemoteAPIAdapter::OnStreamReadable() {
  while (true) {
    debug_ipc::MsgHeader header;
    size_t bytes_read =
        stream_->Peek(reinterpret_cast<char*>(&header), sizeof(header));
    if (bytes_read != sizeof(header))
      return;  // Don't have enough data for the header.
    if (!stream_->IsAvailable(header.size))
      return;  // Entire message hasn't arrived yet.

    // The message size includes the header.
    std::vector<char> buffer(header.size);
    stream_->Read(&buffer[0], header.size);

    // Range check the message type.
    if (header.type == debug_ipc::MsgHeader::Type::kNone ||
        header.type >= debug_ipc::MsgHeader::Type::kNumMessages) {
      fprintf(stderr, "Invalid message type %u, ignoring.\n",
              static_cast<unsigned>(header.type));
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
      DISPATCH(Hello);
      DISPATCH(Launch);
      DISPATCH(Kill);
      DISPATCH(Pause);
      DISPATCH(ProcessTree);
      DISPATCH(Threads);
      DISPATCH(Modules);
      DISPATCH(ReadMemory);
      DISPATCH(Registers);
      DISPATCH(Resume);
      DISPATCH(Detach);
      DISPATCH(AddOrChangeBreakpoint);
      DISPATCH(RemoveBreakpoint);
      DISPATCH(Backtrace);
      DISPATCH(AddressSpace);

      // Attach is special (see remote_api.h): forward the raw data instead of
      // a deserizlied version.
      case debug_ipc::MsgHeader::Type::kAttach:
        api_->OnAttach(std::move(buffer));
        break;

      // Explicitly no "default" to get warnings about unhandled message types,
      // but need to handle these "not a message" types to avoid this warning.
      case debug_ipc::MsgHeader::Type::kNone:
      case debug_ipc::MsgHeader::Type::kNumMessages:
      case debug_ipc::MsgHeader::Type::kNotifyProcessExiting:
      case debug_ipc::MsgHeader::Type::kNotifyThreadStarting:
      case debug_ipc::MsgHeader::Type::kNotifyThreadExiting:
      case debug_ipc::MsgHeader::Type::kNotifyException:
      case debug_ipc::MsgHeader::Type::kNotifyModules:
        break;  // Avoid warning
    }

#undef DISPATCH
  }
}

}  // namespace debug_agent
