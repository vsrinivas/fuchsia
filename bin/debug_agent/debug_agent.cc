// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debug_agent.h"

#include <zircon/syscalls/exception.h>

#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/stream_buffer.h"

namespace {

// Deserializes the request based on type, calls the given hander in the
// DebugAgent, and then sends the reply back.
template<typename RequestMsg, typename ReplyMsg>
void DispatchMessage(DebugAgent* agent,
                     void (DebugAgent::*handler)(const RequestMsg&, ReplyMsg*),
                     std::vector<char> data,
                     const char* type_string) {
  debug_ipc::MessageReader reader(std::move(data));

  RequestMsg request;
  uint32_t transaction_id = 0;
  if (!debug_ipc::ReadRequest(&reader, &request, &transaction_id)) {
    fprintf(stderr, "Got bad debugger %sRequest, ignoring.\n", type_string);
    return;
  }

  ReplyMsg reply;
  (agent->*handler)(request, &reply);

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);

  agent->stream().Write(writer.MessageComplete());
}

}  // namespace

DebugAgent::DebugAgent(ExceptionHandler* handler) : handler_(handler) {}

DebugAgent::~DebugAgent() {}

void DebugAgent::OnStreamData() {
  debug_ipc::MsgHeader header;
  size_t bytes_read = stream().Peek(
      reinterpret_cast<char*>(&header), sizeof(header));
  if (bytes_read != sizeof(header))
    return;  // Don't have enough data for the header.
  if (!stream().IsAvailable(header.size))
    return;  // Entire message hasn't arrived yet.

  // The message size includes the header.
  std::vector<char> buffer(header.size);
  stream().Read(&buffer[0], header.size);

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
  //   OnFoo(FooRequest, FooReply*);
  #define DISPATCH(msg_type) \
    case debug_ipc::MsgHeader::Type::k##msg_type: \
      DispatchMessage<debug_ipc::msg_type##Request, \
                      debug_ipc::msg_type##Reply>( \
                          this, &DebugAgent::On##msg_type, std::move(buffer), \
                          #msg_type); \
      break

  switch (header.type) {
    DISPATCH(Hello);
    DISPATCH(Launch);
    DISPATCH(ProcessTree);
    DISPATCH(Threads);
    DISPATCH(ReadMemory);

    // Explicitly no "default" to get warnings about unhandled message types,
    // but need to handle these "not a message" types to avoid this warning.
    case debug_ipc::MsgHeader::Type::kNone:
    case debug_ipc::MsgHeader::Type::kNumMessages:
      break;  // Avoid warning
  }

  #undef DISPATCH
}

void DebugAgent::OnThreadStarting(const zx::thread& thread) {
  fprintf(stderr, "Exception: thread starting.\n");
  thread.resume(ZX_RESUME_EXCEPTION);
}

void DebugAgent::OnThreadExiting(const zx::thread& thread) {
  fprintf(stderr, "Exception: thread exiting.\n");
  thread.resume(ZX_RESUME_EXCEPTION);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request,
                         debug_ipc::HelloReply* reply) {
  reply->version = 1;
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request,
                          debug_ipc::LaunchReply* reply) {
}

void DebugAgent::OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                               debug_ipc::ProcessTreeReply* reply) {
}

void DebugAgent::OnThreads(const debug_ipc::ThreadsRequest& request,
                           debug_ipc::ThreadsReply* reply) {
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
}
