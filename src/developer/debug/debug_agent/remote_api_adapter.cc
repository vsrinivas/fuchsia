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
                     std::vector<char> data, const char* type_string, uint32_t version) {
  RequestMsg request;

  uint32_t transaction_id = 0;
  if (!debug_ipc::Deserialize(std::move(data), &request, &transaction_id, version)) {
    LOGS(Error) << "Got bad debugger " << type_string << "Request, ignoring.";
    return;
  }

  ReplyMsg reply;
  (adapter->api()->*handler)(request, &reply);

  adapter->stream()->Write(debug_ipc::Serialize(reply, transaction_id, version));
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

    switch (header.type) {
// Dispatches a message type assuming the handler function name, request
// struct type, and reply struct type are all based on the message type name.
// For example, MsgHeader::Type::kFoo will call:
//   api->OnFoo(FooRequest, FooReply*);
#define DISPATCH(msg_type)                                                                 \
  case debug_ipc::MsgHeader::Type::k##msg_type:                                            \
    DispatchMessage<debug_ipc::msg_type##Request, debug_ipc::msg_type##Reply>(             \
        this, &RemoteAPI::On##msg_type, std::move(buffer), #msg_type, api_->GetVersion()); \
    break;

      FOR_EACH_REQUEST_TYPE(DISPATCH)
#undef DISPATCH

      default:
        // Unknown message type.
        LOGS(Error) << "Invalid message type " << static_cast<uint32_t>(header.type)
                    << ", ignoring.";
        break;
    }
  }
}

}  // namespace debug_agent
