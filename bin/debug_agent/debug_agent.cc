// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debug_agent.h"

#include <inttypes.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/launcher.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/process_info.h"
#include "garnet/bin/debug_agent/system_info.h"
#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/stream_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

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
    DISPATCH(Pause);
    DISPATCH(ProcessTree);
    DISPATCH(Threads);
    DISPATCH(ReadMemory);
    DISPATCH(Resume);
    DISPATCH(Detach);
    DISPATCH(AddOrChangeBreakpoint);
    DISPATCH(RemoveBreakpoint);

    // Attach and detach are special because they need to follow the reply immediately with
    // a series of notifications about the current threads. This means they
    // can't use the automatic reply sending of the DispatchMessage function.
    case debug_ipc::MsgHeader::Type::kAttach:
      OnAttach(std::move(buffer));
      break;

    // Explicitly no "default" to get warnings about unhandled message types,
    // but need to handle these "not a message" types to avoid this warning.
    case debug_ipc::MsgHeader::Type::kNone:
    case debug_ipc::MsgHeader::Type::kNumMessages:
    case debug_ipc::MsgHeader::Type::kNotifyProcessExiting:
    case debug_ipc::MsgHeader::Type::kNotifyThreadStarting:
    case debug_ipc::MsgHeader::Type::kNotifyThreadExiting:
    case debug_ipc::MsgHeader::Type::kNotifyException:
      break;  // Avoid warning
  }

  #undef DISPATCH
}

void DebugAgent::OnProcessTerminated(zx_koid_t process_koid) {
  DebuggedProcess* debugged = GetDebuggedProcess(process_koid);
  if (!debugged) {
    FXL_NOTREACHED();
    return;
  }

  debug_ipc::NotifyProcess notify;
  notify.process_koid = process_koid;

  zx_info_process info;
  GetProcessInfo(debugged->process().get(), &info);
  notify.return_code = info.return_code;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcess(notify, &writer);
  stream().Write(writer.MessageComplete());

  RemoveDebuggedProcess(process_koid);
}

void DebugAgent::OnThreadStarting(zx::thread thread, zx_koid_t process_koid,
                                  zx_koid_t thread_koid) {
  DebuggedProcess* debugged = GetDebuggedProcess(process_koid);
  if (!debugged) {
    FXL_NOTREACHED();
    return;
  }

  zx::thread dup_thread;
  thread.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_thread);
  debugged->OnThreadStarting(std::move(dup_thread), thread_koid);

  debug_ipc::ThreadRecord record;
  FillThreadRecord(thread, &record);
  SendThreadNotification(process_koid, record);

  // The thread will currently be in a suspended state, resume it.
  thread.resume(ZX_RESUME_EXCEPTION);
}

void DebugAgent::OnThreadExiting(zx_koid_t proc_koid, zx_koid_t thread_koid) {
  DebuggedProcess* debugged = GetDebuggedProcess(proc_koid);
  if (!debugged) {
    FXL_NOTREACHED();
    return;
  }
  debugged->OnThreadExiting(thread_koid);

  // Can't call FillThreadRecord since the thread doesn't exist any more.
  debug_ipc::NotifyThread notify;
  notify.process_koid = proc_koid;
  notify.record.koid = thread_koid;
  notify.record.state = debug_ipc::ThreadRecord::State::kDead;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadExiting,
                               notify, &writer);
  stream().Write(writer.MessageComplete());
}

void DebugAgent::OnException(zx_koid_t proc_koid, zx_koid_t thread_koid,
                             uint32_t type) {
  // Suspend all threads in the excepting process.
  DebuggedProcess* proc = GetDebuggedProcess(proc_koid);
  if (!proc) {
    FXL_NOTREACHED();
    return;
  }
  proc->OnException(thread_koid, type);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request,
                         debug_ipc::HelloReply* reply) {
  reply->version = 1;
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request,
                          debug_ipc::LaunchReply* reply) {
  Launcher launcher;
  reply->status = launcher.Setup(request.argv);
  if (reply->status != ZX_OK)
    return;

  zx::process process = launcher.GetProcess();
  reply->process_koid = KoidForObject(process);
  reply->process_name = NameForObject(process);
  AddDebuggedProcess(reply->process_koid, std::move(process));

  reply->status = launcher.Start();
  if (reply->status != ZX_OK) {
    RemoveDebuggedProcess(reply->process_koid);
    reply->process_koid = 0;
  }
}

void DebugAgent::OnAttach(std::vector<char> serialized) {
  debug_ipc::MessageReader reader(std::move(serialized));
  debug_ipc::AttachRequest request;
  uint32_t transaction_id = 0;
  if (!debug_ipc::ReadRequest(&reader, &request, &transaction_id)) {
    fprintf(stderr, "Got bad debugger attach request, ignoring.\n");
    return;
  }

  // Don't return early since we must send the reply at the bottom.
  debug_ipc::AttachReply reply;

  zx::process process = GetProcessFromKoid(request.koid);
  zx_handle_t process_handle = process.get();
  if (process.is_valid()) {
    reply.process_name = NameForObject(process);
    DebuggedProcess* new_process =
        AddDebuggedProcess(request.koid, std::move(process));
    new_process->PopulateCurrentThreads();
    reply.status = ZX_OK;
  } else {
    reply.status = ZX_ERR_NOT_FOUND;
  }

  // Send the reply.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);
  stream().Write(writer.MessageComplete());

  // For valid attaches, follow up with the current thread list.
  if (process_handle)
    SendCurrentThreads(process_handle, request.koid);
}

void DebugAgent::OnDetach(const debug_ipc::DetachRequest& request,
                          debug_ipc::DetachReply* reply) {
  auto debug_process = GetDebuggedProcess(request.process_koid);

  if (debug_process->process().is_valid()) {
    RemoveDebuggedProcess(request.process_koid);
    reply->status = ZX_OK;
  } else {
    reply->status = ZX_ERR_NOT_FOUND;
  }
}

void DebugAgent::OnPause(const debug_ipc::PauseRequest& request,
                          debug_ipc::PauseReply* reply) {
  if (request.process_koid) {
    // Single process.
    DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
    if (proc)
      proc->OnPause(request);
  } else {
    // All debugged processes.
    for (const auto& pair : procs_)
      pair.second->OnPause(request);
  }
}

void DebugAgent::OnResume(const debug_ipc::ResumeRequest& request,
                          debug_ipc::ResumeReply* reply) {
  if (request.process_koid) {
    // Single process.
    DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
    if (proc)
      proc->OnResume(request);
  } else {
    // All debugged processes.
    for (const auto& pair : procs_)
      pair.second->OnResume(request);
  }
}

void DebugAgent::OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                               debug_ipc::ProcessTreeReply* reply) {
  GetProcessTree(&reply->root);
}

void DebugAgent::OnThreads(const debug_ipc::ThreadsRequest& request,
                           debug_ipc::ThreadsReply* reply) {
  auto found = procs_.find(request.process_koid);
  if (found == procs_.end())
    return;
  GetProcessThreads(found->second->process().get(), &reply->threads);
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnReadMemory(request, reply);
}

void DebugAgent::OnAddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    debug_ipc::AddOrChangeBreakpointReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc) {
    proc->OnAddOrChangeBreakpoint(request, reply);
  } else {
    reply->status = ZX_ERR_NOT_FOUND;
    reply->error_message = fxl::StringPrintf(
        "Unknown process ID %" PRIu64 ".", request.process_koid);
  }
}

void DebugAgent::OnRemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    debug_ipc::RemoveBreakpointReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnRemoveBreakpoint(request, reply);
}

DebuggedProcess* DebugAgent::GetDebuggedProcess(zx_koid_t koid) {
  auto found = procs_.find(koid);
  if (found == procs_.end())
    return nullptr;
  return found->second.get();
}

DebuggedProcess* DebugAgent::AddDebuggedProcess(zx_koid_t koid, zx::process proc) {
  handler_->Attach(koid, proc.get());
  DebuggedProcess* proc_ptr = new DebuggedProcess(this, koid, std::move(proc));
  procs_[koid] = std::unique_ptr<DebuggedProcess>(proc_ptr);
  return proc_ptr;
}

void DebugAgent::RemoveDebuggedProcess(zx_koid_t koid) {
  auto found = procs_.find(koid);
  if (found == procs_.end())
    return;

  handler_->Detach(found->second->koid());
  procs_.erase(found);
}

void DebugAgent::SendCurrentThreads(zx_handle_t process,
                                    zx_koid_t proc_koid) {
  std::vector<debug_ipc::ThreadRecord> threads;
  GetProcessThreads(process, &threads);
  for (const auto& thread : threads)
    SendThreadNotification(proc_koid, thread);
}

void DebugAgent::SendThreadNotification(zx_koid_t proc_koid,
                                        const debug_ipc::ThreadRecord& record) {
  debug_ipc::NotifyThread notify;
  notify.process_koid = proc_koid;
  notify.record = record;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadStarting,
                               notify, &writer);
  stream().Write(writer.MessageComplete());
}
