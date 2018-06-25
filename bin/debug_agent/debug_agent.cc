// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debug_agent.h"

#include <inttypes.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/bin/debug_agent/launcher.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/process_breakpoint.h"
#include "garnet/bin/debug_agent/process_info.h"
#include "garnet/bin/debug_agent/system_info.h"
#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debug_agent {

DebugAgent::DebugAgent(debug_ipc::StreamBuffer* stream) : stream_(stream) {}

DebugAgent::~DebugAgent() {}

void DebugAgent::RemoveDebuggedProcess(zx_koid_t process_koid) {
  auto found = procs_.find(process_koid);
  if (found == procs_.end())
    FXL_NOTREACHED();
  else
    procs_.erase(found);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request,
                         debug_ipc::HelloReply* reply) {
  // Version and signature are default-initialized to their current values.
  reply->arch = arch::GetArch();
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request,
                          debug_ipc::LaunchReply* reply) {
  Launcher launcher;
  reply->status = launcher.Setup(request.argv);
  if (reply->status != ZX_OK)
    return;

  zx::process process = launcher.GetProcess();
  zx_koid_t process_koid = KoidForObject(process);

  DebuggedProcess* debugged_process =
      AddDebuggedProcess(process_koid, std::move(process));
  if (!debugged_process)
    return;

  reply->status = launcher.Start();
  if (reply->status != ZX_OK) {
    RemoveDebuggedProcess(process_koid);
    return;
  }

  // Success, fill out the reply.
  reply->process_koid = process_koid;
  reply->process_name = NameForObject(process);
}

void DebugAgent::OnKill(const debug_ipc::KillRequest& request,
                        debug_ipc::KillReply* reply) {
  auto debug_process = GetDebuggedProcess(request.process_koid);

  if (!debug_process || !debug_process->process().is_valid()) {
    reply->status = ZX_ERR_NOT_FOUND;
    return;
  }
  debug_process->OnKill(request, reply);
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
  reply.status = ZX_ERR_NOT_FOUND;
  zx::process process = GetProcessFromKoid(request.koid);
  DebuggedProcess* new_process = nullptr;
  if (process.is_valid()) {
    reply.process_name = NameForObject(process);
    new_process = AddDebuggedProcess(request.koid, std::move(process));
    if (new_process)
      reply.status = ZX_OK;
  }

  // Send the reply.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);
  stream()->Write(writer.MessageComplete());

  // For valid attaches, follow up with the current thread list.
  if (new_process)
    new_process->PopulateCurrentThreads();
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

void DebugAgent::OnModules(const debug_ipc::ModulesRequest& request,
                           debug_ipc::ModulesReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    GetModulesForProcess(proc->process(), &reply->modules);
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

void DebugAgent::OnRegisters(
    const debug_ipc::RegistersRequest& request,
    debug_ipc::RegistersReply* reply) {
  DebuggedThread *thread = GetDebuggedThread(request.process_koid,
                                             request.thread_koid);
  if (thread)
    thread->GetRegisters(&reply->registers);
}

void DebugAgent::OnAddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    debug_ipc::AddOrChangeBreakpointReply* reply) {
  uint32_t id = request.breakpoint.breakpoint_id;

  auto found = breakpoints_.find(id);
  if (found == breakpoints_.end()) {
    found = breakpoints_
                .emplace(std::piecewise_construct, std::forward_as_tuple(id),
                         std::forward_as_tuple(this))
                .first;
  }
  reply->status = found->second.SetSettings(request.breakpoint);
}

void DebugAgent::OnRemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    debug_ipc::RemoveBreakpointReply* reply) {
  auto found = breakpoints_.find(request.breakpoint_id);
  if (found != breakpoints_.end())
    breakpoints_.erase(found);
}

void DebugAgent::OnBacktrace(const debug_ipc::BacktraceRequest& request,
                             debug_ipc::BacktraceReply* reply) {
  DebuggedThread* thread =
      GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread)
    thread->GetBacktrace(&reply->frames);
}

zx_status_t DebugAgent::RegisterBreakpoint(Breakpoint* bp,
                                           zx_koid_t process_koid,
                                           uint64_t address) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterBreakpoint(bp, address);

  // The process might legitimately be not found if there was a race between
  // the process terminating and a breakpoint add/change.
  return ZX_ERR_NOT_FOUND;
}

void DebugAgent::UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                      uint64_t address) {
  // The process might legitimately be not found if it was terminated.
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    proc->UnregisterBreakpoint(bp, address);
}

void DebugAgent::OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                                debug_ipc::AddressSpaceReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnAddressSpace(request, reply);
}

DebuggedProcess* DebugAgent::GetDebuggedProcess(zx_koid_t koid) {
  auto found = procs_.find(koid);
  if (found == procs_.end())
    return nullptr;
  return found->second.get();
}

DebuggedThread* DebugAgent::GetDebuggedThread(zx_koid_t process_koid,
                                              zx_koid_t thread_koid) {
  DebuggedProcess* process = GetDebuggedProcess(process_koid);
  if (!process)
    return nullptr;
  return process->GetThread(thread_koid);
}

DebuggedProcess* DebugAgent::AddDebuggedProcess(zx_koid_t process_koid,
                                                zx::process zx_proc) {
  auto proc =
      std::make_unique<DebuggedProcess>(this, process_koid, std::move(zx_proc));
  if (!proc->Init())
    return nullptr;

  DebuggedProcess* result = proc.get();
  procs_[process_koid] = std::move(proc);
  return result;
}

}  // namespace debug_agent
