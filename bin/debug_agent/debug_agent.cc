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
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debug_agent {

DebugAgent::DebugAgent(debug_ipc::StreamBuffer* stream,
                       std::shared_ptr<component::Services> services)
    : stream_(stream), services_(services) {}

DebugAgent::~DebugAgent() = default;

void DebugAgent::OnProcessStart(zx::process process) {
  auto koid = KoidForObject(process);
  auto name = NameForObject(process);

  // Send notification, then create debug process so that thread notification is
  // sent after this
  debug_ipc::NotifyProcessStarting notify;
  notify.koid = koid;
  notify.name = name;
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessStarting(notify, &writer);
  stream()->Write(writer.MessageComplete());

  AddDebuggedProcess(koid, std::move(process), false);
}

void DebugAgent::RemoveDebuggedProcess(zx_koid_t process_koid) {
  auto found = procs_.find(process_koid);
  if (found == procs_.end())
    FXL_NOTREACHED();
  else
    procs_.erase(found);
}

void DebugAgent::RemoveDebuggedJob(zx_koid_t job_koid) {
  auto found = jobs_.find(job_koid);
  if (found == jobs_.end())
    FXL_NOTREACHED();
  else
    jobs_.erase(found);
}

void DebugAgent::RemoveBreakpoint(uint32_t breakpoint_id) {
  auto found = breakpoints_.find(breakpoint_id);
  if (found != breakpoints_.end())
    breakpoints_.erase(found);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request,
                         debug_ipc::HelloReply* reply) {
  // Version and signature are default-initialized to their current values.
  reply->arch = arch::ArchProvider::Get().GetArch();
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request,
                          debug_ipc::LaunchReply* reply) {
  Launcher launcher(services_);
  reply->status = launcher.Setup(request.argv);
  if (reply->status != ZX_OK)
    return;

  zx::process process = launcher.GetProcess();
  zx_koid_t process_koid = KoidForObject(process);

  // TODO(donosoc): change resume thread setting once we have global settings.
  DebuggedProcess* debugged_process =
      AddDebuggedProcess(process_koid, std::move(process), true);
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
    FXL_LOG(WARNING) << "Got bad debugger attach request, ignoring.";
    return;
  }

  // Don't return early since we must send the reply at the bottom.
  debug_ipc::AttachReply reply;
  reply.status = ZX_ERR_NOT_FOUND;
  if (request.type == debug_ipc::AttachRequest::Type::kProcess) {
    zx::process process = GetProcessFromKoid(request.koid);
    DebuggedProcess* new_process = nullptr;
    if (process.is_valid()) {
      reply.name = NameForObject(process);
      reply.koid = request.koid;
      // TODO(donosoc): change resume thread setting once we have global
      // settings.
      new_process = AddDebuggedProcess(request.koid, std::move(process), true);
      if (new_process)
        reply.status = ZX_OK;
    }

    // Send the reply.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteReply(reply, transaction_id, &writer);
    stream()->Write(writer.MessageComplete());

    // For valid attaches, follow up with the current module and thread lists.
    if (new_process) {
      new_process->PopulateCurrentThreads();

      if (new_process->RegisterDebugState()) {
        // Suspend all threads while the module list is being sent. The client
        // will resume the threads once it's loaded symbols and processed
        // breakpoints (this may take a while and we'd like to get any
        // breakpoints as early as possible).
        std::vector<uint64_t> paused_thread_koids;
        new_process->PauseAll(&paused_thread_koids);
        new_process->SendModuleNotification(std::move(paused_thread_koids));
      }
    }
  } else if (request.type == debug_ipc::AttachRequest::Type::kJob) {
    zx::job job = GetJobFromKoid(request.koid);
    if (job.is_valid()) {
      reply.name = NameForObject(job);
      reply.koid = request.koid;
      auto new_job = AddDebuggedJob(request.koid, std::move(job));
      if (new_job) {
        reply.status = ZX_OK;
      }
    }

    // Send the reply.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteReply(reply, transaction_id, &writer);
    stream()->Write(writer.MessageComplete());
  } else if (request.type == debug_ipc::AttachRequest::Type::kComponentRoot) {
    std::string koid_str;
    bool file_read = files::ReadFileToString("/hub/job-id", &koid_str);
    if (!file_read) {
      FXL_LOG(ERROR) << "Not able to read job-id: " << strerror(errno);
      reply.status = ZX_ERR_INTERNAL;
    } else {
      char* end = NULL;
      uint64_t koid = strtoul(koid_str.c_str(), &end, 10);
      if (*end) {
        FXL_LOG(ERROR) << "Invalid job-id: " << koid_str.c_str();
        reply.status = ZX_ERR_INTERNAL;
      } else {
        zx::job job = GetJobFromKoid(koid);
        if (job.is_valid()) {
          reply.koid = koid;
          reply.name = NameForObject(job);
          auto new_job = AddDebuggedJob(koid, std::move(job));
          if (new_job) {
            reply.status = ZX_OK;
          }
        }
      }
    }
    // Send the reply.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteReply(reply, transaction_id, &writer);
    stream()->Write(writer.MessageComplete());
  } else {
    FXL_LOG(WARNING) << "Got bad debugger attach request type, ignoring.";
    return;
  }
}

void DebugAgent::OnDetach(const debug_ipc::DetachRequest& request,
                          debug_ipc::DetachReply* reply) {
  switch (request.type) {
    case debug_ipc::DetachRequest::Type::kJob: {
      auto debug_job = GetDebuggedJob(request.koid);
      if (debug_job && debug_job->job().is_valid()) {
        RemoveDebuggedJob(request.koid);
        reply->status = ZX_OK;
      } else {
        reply->status = ZX_ERR_NOT_FOUND;
      }
      break;
    }
    case debug_ipc::DetachRequest::Type::kProcess: {
      auto debug_process = GetDebuggedProcess(request.koid);
      if (debug_process && debug_process->process().is_valid()) {
        RemoveDebuggedProcess(request.koid);
        reply->status = ZX_OK;
      } else {
        reply->status = ZX_ERR_NOT_FOUND;
      }
      break;
    }
    default:
      reply->status = ZX_ERR_INVALID_ARGS;
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

void DebugAgent::OnQuitAgent(const debug_ipc::QuitAgentRequest& request,
                             debug_ipc::QuitAgentReply* reply) {
  should_quit_ = true;
  debug_ipc::MessageLoop::Current()->QuitNow();
};

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
    proc->OnModules(reply);
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
  GetProcessThreads(found->second->process(), found->second->dl_debug_addr(),
                    &reply->threads);
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnReadMemory(request, reply);
}

void DebugAgent::OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                                 debug_ipc::ReadRegistersReply* reply) {
  DebuggedThread* thread =
      GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    thread->ReadRegisters(request.categories, &reply->categories);
  } else {
    FXL_LOG(ERROR) << "Cannot find thread with koid: " << request.thread_koid;
  }
}

void DebugAgent::OnWriteRegisters(
    const debug_ipc::WriteRegistersRequest& request,
    debug_ipc::WriteRegistersReply* reply) {
  DebuggedThread* thread =
      GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    reply->status = thread->WriteRegisters(request.registers);
  } else {
    reply->status = ZX_ERR_NOT_FOUND;
    FXL_LOG(ERROR) << "Cannot find thread with koid: " << request.thread_koid;
  }
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
  RemoveBreakpoint(request.breakpoint_id);
}

void DebugAgent::OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                                debug_ipc::ThreadStatusReply* reply) {
  DebuggedThread* thread =
      GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kFull,
                             nullptr, &reply->record);
  } else {
    // When the thread is not found the thread record is set to "dead".
    reply->record.koid = request.thread_koid;
    reply->record.state = debug_ipc::ThreadRecord::State::kDead;
  }
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

zx_status_t DebugAgent::RegisterWatchpoint(
    Watchpoint* wp, zx_koid_t process_koid,
    const debug_ipc::AddressRange& range) {
  DebuggedProcess* process = GetDebuggedProcess(process_koid);
  if (!process) {
    // The process might legitimately be not found if there was a race between
    // the process terminating and a watchpoint add/change.
    return ZX_ERR_NOT_FOUND;
  }

  return process->RegisterWatchpoint(wp, range);
}

void DebugAgent::UnregisterWatchpoint(Watchpoint* wp, zx_koid_t process_koid,
                                      const debug_ipc::AddressRange& range) {
  // The process might legitimately be not found if there was a race between
  // the process terminating and a watchpoint add/change.
  DebuggedProcess* process = GetDebuggedProcess(process_koid);
  if (!process)
    return;

  process->UnregisterWatchpoint(wp, range);
}

void DebugAgent::OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                                debug_ipc::AddressSpaceReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnAddressSpace(request, reply);
}

void DebugAgent::OnJobFilter(const debug_ipc::JobFilterRequest& request,
                             debug_ipc::JobFilterReply* reply) {
  DebuggedJob* job = GetDebuggedJob(request.job_koid);
  if (!job) {
    reply->status = ZX_ERR_INVALID_ARGS;
    return;
  }
  job->SetFilters(std::move(request.filters));
  reply->status = ZX_OK;
}

void DebugAgent::OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                               debug_ipc::WriteMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnWriteMemory(request, reply);
  else
    reply->status = ZX_ERR_NOT_FOUND;
}

void DebugAgent::OnSymbolTables(const debug_ipc::SymbolTablesRequest& request,
                                debug_ipc::SymbolTablesReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnSymbolTables(request, reply);
}

DebuggedProcess* DebugAgent::GetDebuggedProcess(zx_koid_t koid) {
  auto found = procs_.find(koid);
  if (found == procs_.end())
    return nullptr;
  return found->second.get();
}

DebuggedJob* DebugAgent::GetDebuggedJob(zx_koid_t koid) {
  auto found = jobs_.find(koid);
  if (found == jobs_.end())
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

DebuggedJob* DebugAgent::AddDebuggedJob(zx_koid_t job_koid, zx::job zx_job) {
  auto job = std::make_unique<DebuggedJob>(this, job_koid, std::move(zx_job));
  if (!job->Init())
    return nullptr;

  DebuggedJob* result = job.get();
  jobs_[job_koid] = std::move(job);
  return result;
}

DebuggedProcess* DebugAgent::AddDebuggedProcess(zx_koid_t process_koid,
                                                zx::process zx_proc,
                                                bool resume_initial_thread) {
  auto proc = std::make_unique<DebuggedProcess>(
      this, process_koid, std::move(zx_proc), resume_initial_thread);
  if (!proc->Init())
    return nullptr;

  DebuggedProcess* result = proc.get();
  procs_[process_koid] = std::move(proc);
  return result;
}

}  // namespace debug_agent
