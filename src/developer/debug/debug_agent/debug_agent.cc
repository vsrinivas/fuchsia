// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/features.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/binary_launcher.h"
#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/debug_agent/time.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

constexpr size_t kMegabyte = 1024 * 1024;

std::string LogResumeRequest(const debug_ipc::ResumeRequest& request) {
  std::stringstream ss;
  ss << "Got resume request for ";

  // Print thread koids.
  if (request.ids.empty()) {
    ss << "all processes.";
  } else {
    for (size_t i = 0; i < request.ids.size(); i++) {
      if (i > 0)
        ss << ", ";
      ss << "(" << request.ids[i].process << ", " << request.ids[i].thread << ")";
    }
  }

  // Print step range.
  if (request.range_begin != request.range_end)
    ss << ", Range: [" << std::hex << request.range_begin << ", " << request.range_end << "]";

  return ss.str();
}

}  // namespace

DebugAgent::DebugAgent(std::unique_ptr<SystemInterface> system_interface)
    : system_interface_(std::move(system_interface)), weak_factory_(this) {
  // Set a callback to the LimboProvider to let us know when new processes enter the limbo.
  system_interface_->GetLimboProvider().set_on_enter_limbo(
      [agent = GetWeakPtr()](const LimboProvider::Record& record) {
        if (!agent)
          return;
        agent->OnProcessEnteredLimbo(record);
      });
}

DebugAgent::~DebugAgent() {
  // Clear the callback to prevent dangling pointers.
  system_interface_->GetLimboProvider().set_on_enter_limbo(LimboProvider::OnEnterLimboCallback());
}

fxl::WeakPtr<DebugAgent> DebugAgent::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void DebugAgent::Connect(debug_ipc::StreamBuffer* stream) {
  FX_DCHECK(!stream_) << "A debug agent should not be connected twice!";
  stream_ = stream;
}

void DebugAgent::Disconnect() {
  FX_DCHECK(stream_);
  stream_ = nullptr;

  // Remove all DebuggedProcess and DebuggedJob on disconnect. The logic here should be consistent
  // with zxdb::System::DidDisconnect() so that further reconnecting won't create any surprise.
  jobs_.clear();
  // Removes breakpoints before we detach from the processes, although it should also be safe to
  // reverse the order.
  breakpoints_.clear();
  // The destructor of the DebuggedProcess will detach us from the process.
  procs_.clear();
}

debug_ipc::StreamBuffer* DebugAgent::stream() {
  FX_DCHECK(stream_);
  return stream_;
}

void DebugAgent::RemoveDebuggedProcess(zx_koid_t process_koid) {
  auto found = procs_.find(process_koid);
  if (found == procs_.end())
    FX_NOTREACHED();
  else
    procs_.erase(found);
}

void DebugAgent::RemoveDebuggedJob(zx_koid_t job_koid) {
  auto found = jobs_.find(job_koid);
  if (found == jobs_.end())
    FX_NOTREACHED();
  else
    jobs_.erase(found);
}

Breakpoint* DebugAgent::GetBreakpoint(uint32_t breakpoint_id) {
  if (auto found = breakpoints_.find(breakpoint_id); found != breakpoints_.end())
    return &found->second;
  return nullptr;
}

void DebugAgent::RemoveBreakpoint(uint32_t breakpoint_id) {
  if (auto found = breakpoints_.find(breakpoint_id); found != breakpoints_.end())
    breakpoints_.erase(found);
}

void DebugAgent::OnConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                               debug_ipc::ConfigAgentReply* reply) {
  reply->results = HandleActions(request.actions, &configuration_);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request, debug_ipc::HelloReply* reply) {
  // Version and signature are default-initialized to their current values.
  reply->arch = arch::GetCurrentArch();
}

void DebugAgent::OnStatus(const debug_ipc::StatusRequest& request, debug_ipc::StatusReply* reply) {
  // Get the attached processes.
  reply->processes.reserve(procs_.size());
  for (auto& [process_koid, proc] : procs_) {
    debug_ipc::ProcessRecord process_record = {};
    process_record.process_koid = process_koid;
    process_record.process_name = proc->process_handle().GetName();

    auto threads = proc->GetThreads();
    process_record.threads.reserve(threads.size());
    for (auto* thread : threads) {
      process_record.threads.emplace_back(
          thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kNone));
    }

    reply->processes.emplace_back(std::move(process_record));
  }

  // Get the limbo processes.
  if (system_interface_->GetLimboProvider().Valid()) {
    for (const auto& [process_koid, record] :
         system_interface_->GetLimboProvider().GetLimboRecords()) {
      debug_ipc::ProcessRecord process_record = {};
      process_record.process_koid = process_koid;
      process_record.process_name = record.process->GetName();

      // For now, only fill the thread blocked on exception.
      process_record.threads.push_back(record.thread->GetThreadRecord(process_koid));

      reply->limbo.push_back(std::move(process_record));
    }
  }
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request, debug_ipc::LaunchReply* reply) {
  switch (request.inferior_type) {
    case debug_ipc::InferiorType::kBinary:
      LaunchProcess(request, reply);
      return;
    case debug_ipc::InferiorType::kComponent:
      LaunchComponent(request, reply);
      return;
    case debug_ipc::InferiorType::kLast:
      break;
  }

  reply->timestamp = GetNowTimestamp();
  reply->status = debug::Status("Invalid inferior type to launch.");
}

void DebugAgent::OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply) {
  reply->timestamp = GetNowTimestamp();
  // See first if the process is in limbo.
  LimboProvider& limbo = system_interface_->GetLimboProvider();
  if (limbo.Valid() && limbo.IsProcessInLimbo(request.process_koid)) {
    // Release if from limbo, which will effectivelly kill it.
    reply->status = limbo.ReleaseProcess(request.process_koid);
    return;
  }

  // Otherwise search locally.
  auto debug_process = GetDebuggedProcess(request.process_koid);
  if (!debug_process) {
    reply->status = debug::Status("Process is not currently being debugged.");
    return;
  }

  debug_process->OnKill(request, reply);

  // Check if this was a limbo "kill". If so, mark this process to be removed from limbo when it
  // re-enters it and tell the client that we successfully killed it.
  if (reply->status.has_error() && debug_process->from_limbo()) {
    killed_limbo_procs_.insert(debug_process->koid());
    reply->status = debug::Status();
  }

  RemoveDebuggedProcess(request.process_koid);
}

void DebugAgent::OnDetach(const debug_ipc::DetachRequest& request, debug_ipc::DetachReply* reply) {
  reply->timestamp = GetNowTimestamp();
  switch (request.type) {
    case debug_ipc::TaskType::kJob: {
      if (auto debug_job = GetDebuggedJob(request.koid)) {
        RemoveDebuggedJob(request.koid);
        reply->status = debug::Status();
      } else {
        reply->status = debug::Status("Not currently attached to job " +
                                      std::to_string(request.koid) + " to detach from.");
      }
      break;
    }
    case debug_ipc::TaskType::kProcess: {
      // First check if the process is waiting in limbo. If so, release it.
      LimboProvider& limbo = system_interface_->GetLimboProvider();
      if (limbo.Valid() && limbo.IsProcessInLimbo(request.koid)) {
        reply->status = limbo.ReleaseProcess(request.koid);
        return;
      }

      auto debug_process = GetDebuggedProcess(request.koid);
      if (debug_process && debug_process->handle().is_valid()) {
        RemoveDebuggedProcess(request.koid);
        reply->status = debug::Status();
      } else {
        reply->status = debug::Status("Not currently attached to process " +
                                      std::to_string(request.koid) + " to detach from.");
      }
      break;
    }
    default:
      reply->status = debug::Status("Bad task type to detach from.");
  }
}

void DebugAgent::OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply) {
  std::vector<debug_ipc::ProcessThreadId> proc_thread_pairs;

  if (request.id.process) {
    // Single process.
    if (DebuggedProcess* proc = GetDebuggedProcess(request.id.process)) {
      if (request.id.thread) {
        // Single thread of the process.
        if (DebuggedThread* thread = proc->GetThread(request.id.thread)) {
          thread->ClientSuspend(true);
          proc_thread_pairs.push_back(request.id);
        }
      } else {
        // All threads of that process.
        auto suspended_thread_koids = proc->ClientSuspendAllThreads();
        for (zx_koid_t thread_koid : suspended_thread_koids) {
          proc_thread_pairs.push_back({.process = request.id.process, .thread = thread_koid});
        }
      }
    }
  } else {
    // All debugged processes.
    proc_thread_pairs = ClientSuspendAll();
  }

  // Save the affected thread info.
  for (const auto& id : proc_thread_pairs) {
    if (DebuggedThread* thread = GetDebuggedThread(id)) {
      reply->threads.push_back(
          thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
    }
  }
}

void DebugAgent::OnQuitAgent(const debug_ipc::QuitAgentRequest& request,
                             debug_ipc::QuitAgentReply* reply) {
  debug_ipc::MessageLoop::Current()->QuitNow();
};

void DebugAgent::OnResume(const debug_ipc::ResumeRequest& request, debug_ipc::ResumeReply* reply) {
  DEBUG_LOG(Agent) << LogResumeRequest(request);

  if (request.ids.empty()) {
    // All debugged processes.
    for (const auto& pair : procs_)
      pair.second->OnResume(request);
  } else {
    // Explicit list.
    for (const auto& id : request.ids) {
      if (DebuggedProcess* proc = GetDebuggedProcess(id.process)) {
        if (id.thread) {
          // Single thread in that process.
          if (DebuggedThread* thread = proc->GetThread(id.thread)) {
            thread->ClientResume(request);
          } else {
            FX_LOGS(WARNING) << "Could not find thread by koid: " << id.thread;
          }
        } else {
          // All threads in the process.
          proc->OnResume(request);
        }
      } else {
        FX_LOGS(WARNING) << "Could not find process by koid: " << id.process;
      }
    }
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
  reply->root = system_interface_->GetProcessTree();
}

void DebugAgent::OnThreads(const debug_ipc::ThreadsRequest& request,
                           debug_ipc::ThreadsReply* reply) {
  auto found = procs_.find(request.process_koid);
  if (found == procs_.end())
    return;

  reply->threads = found->second->GetThreadRecords();
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnReadMemory(request, reply);
}

void DebugAgent::OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                                 debug_ipc::ReadRegistersReply* reply) {
  DebuggedThread* thread = GetDebuggedThread(request.id);
  if (thread) {
    reply->registers = thread->ReadRegisters(request.categories);
  } else {
    FX_LOGS(ERROR) << "Cannot find thread with koid: " << request.id.thread;
  }
}

void DebugAgent::OnWriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                                  debug_ipc::WriteRegistersReply* reply) {
  DebuggedThread* thread = GetDebuggedThread(request.id);
  if (thread) {
    reply->status = debug::Status();
    reply->registers = thread->WriteRegisters(request.registers);
  } else {
    reply->status = debug::Status("Can not find thread " + std::to_string(request.id.thread) +
                                  " to write registers.");
    FX_LOGS(ERROR) << "Cannot find thread with koid: " << request.id.thread;
  }
}

void DebugAgent::OnAddOrChangeBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                                         debug_ipc::AddOrChangeBreakpointReply* reply) {
  switch (request.breakpoint.type) {
    case debug_ipc::BreakpointType::kSoftware:
    case debug_ipc::BreakpointType::kHardware:
    case debug_ipc::BreakpointType::kReadWrite:
    case debug_ipc::BreakpointType::kWrite:
      return SetupBreakpoint(request, reply);
    case debug_ipc::BreakpointType::kLast:
      break;
  }

  FX_NOTREACHED() << "Invalid Breakpoint Type: " << static_cast<int>(request.breakpoint.type);
}

void DebugAgent::OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                                    debug_ipc::RemoveBreakpointReply* reply) {
  RemoveBreakpoint(request.breakpoint_id);
}

void DebugAgent::OnSysInfo(const debug_ipc::SysInfoRequest& request,
                           debug_ipc::SysInfoReply* reply) {
  reply->version = system_interface_->GetSystemVersion();
  reply->num_cpus = system_interface_->GetNumCpus();
  reply->memory_mb = system_interface_->GetPhysicalMemory() / kMegabyte;

  reply->hw_breakpoint_count = arch::GetHardwareBreakpointCount();
  reply->hw_watchpoint_count = arch::GetHardwareWatchpointCount();
}

void DebugAgent::OnProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                                 debug_ipc::ProcessStatusReply* reply) {
  auto it = procs_.find(request.process_koid);
  if (it == procs_.end()) {
    reply->status =
        debug::Status("Not currently attached to process " + std::to_string(request.process_koid));
    return;
  }

  DebuggedProcess* process = it->second.get();

  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [this, process]() mutable {
    debug_ipc::NotifyProcessStarting notify = {};
    notify.koid = process->koid();
    notify.name = process->process_handle().GetName();
    notify.timestamp = GetNowTimestamp();

    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyProcessStarting(notify, &writer);
    stream()->Write(writer.MessageComplete());

    // Send the modules notification.
    process->SuspendAndSendModulesIfKnown();
  });

  reply->status = debug::Status();
}

void DebugAgent::OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                                debug_ipc::ThreadStatusReply* reply) {
  if (DebuggedThread* thread = GetDebuggedThread(request.id)) {
    reply->record = thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kFull);
  } else {
    // When the thread is not found the thread record is set to "dead".
    reply->record.id = request.id;
    reply->record.state = debug_ipc::ThreadRecord::State::kDead;
  }
}

debug::Status DebugAgent::RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                             uint64_t address) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterBreakpoint(bp, address);

  // The process might legitimately be not found if there was a race between
  // the process terminating and a breakpoint add/change.
  return debug::Status("Process not found when adding breakpoint");
}

void DebugAgent::UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) {
  // The process might legitimately be not found if it was terminated.
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    proc->UnregisterBreakpoint(bp, address);
}

void DebugAgent::SetupBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                                 debug_ipc::AddOrChangeBreakpointReply* reply) {
  uint32_t id = request.breakpoint.id;
  auto found = breakpoints_.find(id);
  if (found == breakpoints_.end()) {
    DEBUG_LOG(Agent) << "Creating new breakpoint " << request.breakpoint.id << " ("
                     << request.breakpoint.name << ").";
    found = breakpoints_
                .emplace(std::piecewise_construct, std::forward_as_tuple(id),
                         std::forward_as_tuple(this))
                .first;
  }

  reply->status = found->second.SetSettings(request.breakpoint);
}

debug::Status DebugAgent::RegisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                             const debug_ipc::AddressRange& range) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterWatchpoint(bp, range);

  // The process might legitimately be not found if there was a race between the process terminating
  // and a breakpoint add/change.
  return debug::Status("Process not found when adding watchpoint");
}

void DebugAgent::UnregisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                      const debug_ipc::AddressRange& range) {
  // The process might legitimately be not found if it was terminated.
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    proc->UnregisterWatchpoint(bp, range);
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
    reply->status = debug::Status("Not attached to job " + std::to_string(request.job_koid) +
                                  " while adding a filter.");
    return;
  }

  for (const auto& match : job->SetFilters(std::move(request.filters)))
    reply->matched_processes.push_back(match->GetKoid());
  reply->status = debug::Status();
}

void DebugAgent::OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                               debug_ipc::WriteMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc) {
    proc->OnWriteMemory(request, reply);
  } else {
    reply->status = debug::Status("Not attached to process " +
                                  std::to_string(request.process_koid) + " while writing memory.");
  }
}

void DebugAgent::OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                                       debug_ipc::LoadInfoHandleTableReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnLoadInfoHandleTable(request, reply);
  else
    reply->status =
        debug::Status("Not attached to process " + std::to_string(request.process_koid) +
                      " while getting the handle table.");
}

void DebugAgent::OnUpdateGlobalSettings(const debug_ipc::UpdateGlobalSettingsRequest& request,
                                        debug_ipc::UpdateGlobalSettingsReply* reply) {
  for (const auto& update : request.exception_strategies) {
    exception_strategies_[update.type] = update.value;
  }
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

DebuggedThread* DebugAgent::GetDebuggedThread(const debug_ipc::ProcessThreadId& id) {
  DebuggedProcess* process = GetDebuggedProcess(id.process);
  if (!process)
    return nullptr;
  return process->GetThread(id.thread);
}

std::vector<debug_ipc::ProcessThreadId> DebugAgent::ClientSuspendAll(zx_koid_t except_process,
                                                                     zx_koid_t except_thread) {
  // Neither or both koids must be supplied.
  FX_DCHECK((except_process == ZX_KOID_INVALID && except_thread == ZX_KOID_INVALID) ||
            (except_process != ZX_KOID_INVALID && except_thread != ZX_KOID_INVALID));

  std::vector<debug_ipc::ProcessThreadId> affected;

  for (const auto& [process_koid, process] : procs_) {
    std::vector<zx_koid_t> affected_threads;
    if (process_koid == except_process) {
      affected_threads = process->ClientSuspendAllThreads(except_thread);
    } else {
      affected_threads = process->ClientSuspendAllThreads();
    }

    for (zx_koid_t thread_koid : affected_threads) {
      affected.push_back({.process = process_koid, .thread = thread_koid});
    }
  }

  return affected;
}

debug::Status DebugAgent::AddDebuggedJob(std::unique_ptr<JobHandle> job_handle) {
  zx_koid_t koid = job_handle->GetKoid();
  auto job = std::make_unique<DebuggedJob>(this, std::move(job_handle));
  if (auto status = job->Init(); status.has_error())
    return status;

  jobs_[koid] = std::move(job);
  return debug::Status();
}

debug::Status DebugAgent::AddDebuggedProcess(DebuggedProcessCreateInfo&& create_info,
                                             DebuggedProcess** new_process) {
  *new_process = nullptr;

  auto proc = std::make_unique<DebuggedProcess>(this, std::move(create_info));
  if (auto status = proc->Init(); status.has_error())
    return status;

  auto process_id = proc->koid();
  *new_process = proc.get();
  procs_[process_id] = std::move(proc);
  return debug::Status();
}

debug_ipc::ExceptionStrategy DebugAgent::GetExceptionStrategy(debug_ipc::ExceptionType type) {
  auto strategy = exception_strategies_.find(type);
  if (strategy == exception_strategies_.end()) {
    return debug_ipc::ExceptionStrategy::kFirstChance;
  }
  return strategy->second;
}

// Attaching ---------------------------------------------------------------------------------------

namespace {

void SendAttachReply(DebugAgent* debug_agent, uint32_t transaction_id, const debug::Status& status,
                     zx_koid_t process_koid = ZX_HANDLE_INVALID,
                     const std::string& process_name = "") {
  debug_ipc::AttachReply reply;
  reply.status = status;
  reply.koid = process_koid;
  reply.name = process_name;
  reply.timestamp = GetNowTimestamp();

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);
  debug_agent->stream()->Write(writer.MessageComplete());
}

}  // namespace

void DebugAgent::OnAttach(std::vector<char> serialized) {
  debug_ipc::MessageReader reader(std::move(serialized));
  debug_ipc::AttachRequest request;
  uint32_t transaction_id = 0;
  if (!debug_ipc::ReadRequest(&reader, &request, &transaction_id)) {
    FX_LOGS(WARNING) << "Got bad debugger attach request, ignoring.";
    return;
  }

  OnAttach(transaction_id, request);
}

void DebugAgent::OnAttach(uint32_t transaction_id, const debug_ipc::AttachRequest& request) {
  if (request.type == debug_ipc::TaskType::kProcess) {
    AttachToProcess(request.koid, transaction_id);
    return;
  }

  // All other attach types are variants of job attaches.
  std::unique_ptr<JobHandle> job;
  if (request.type == debug_ipc::TaskType::kJob) {
    job = system_interface_->GetJob(request.koid);
  } else if (request.type == debug_ipc::TaskType::kComponentRoot) {
    job = system_interface_->GetComponentRootJob();
    if (job)
      attached_root_job_koid_ = job->GetKoid();
  } else if (request.type == debug_ipc::TaskType::kSystemRoot) {
    job = system_interface_->GetRootJob();
    if (job)
      attached_root_job_koid_ = job->GetKoid();
  } else {
    FX_LOGS(WARNING) << "Got bad debugger attach request type, ignoring.";
    return;
  }

  debug_ipc::AttachReply reply;

  // Don't return early since we always need to send the reply, even on fail.
  if (job) {
    DEBUG_LOG(Agent) << "Attaching to job " << job->GetKoid();

    reply.name = job->GetName();
    reply.koid = job->GetKoid();
    reply.status = AddDebuggedJob(std::move(job));
    reply.timestamp = GetNowTimestamp();
  } else {
    DEBUG_LOG(Agent) << "Failed to attach to job.";
    reply.status = debug::Status("Could not attach to job.");
  }

  // Send the reply.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);
  stream()->Write(writer.MessageComplete());
}

void DebugAgent::AttachToProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  DEBUG_LOG(Agent) << "Attemping to attach to process " << process_koid;

  // See if we're already attached to this process.
  for (auto& [koid, proc] : procs_) {
    if (koid == process_koid) {
      debug::Status error(debug::Status::kAlreadyExists,
                          "Already attached to process " + std::to_string(proc->koid()));
      DEBUG_LOG(Agent) << error.message();
      SendAttachReply(this, transaction_id, error);
      return;
    }
  }

  // First check if the process is in limbo. Sends the appropiate replies/notifications.
  if (system_interface_->GetLimboProvider().Valid()) {
    auto status = AttachToLimboProcess(process_koid, transaction_id);
    if (status.ok())
      return;

    DEBUG_LOG(Agent) << "Could not attach to process in limbo: " << status.message();
  }

  // Attempt to attach to an existing process. Sends the appropriate replies/notifications.
  auto status = AttachToExistingProcess(process_koid, transaction_id);
  if (status.ok())
    return;

  DEBUG_LOG(Agent) << "Could not attach to existing process: " << status.message();

  // We didn't find a process.
  SendAttachReply(this, transaction_id, status);
}

debug::Status DebugAgent::AttachToLimboProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  LimboProvider& limbo = system_interface_->GetLimboProvider();
  FX_DCHECK(limbo.Valid());

  // Obtain the process and exception from limbo.
  auto retrieved = limbo.RetrieveException(process_koid);
  if (retrieved.is_error()) {
    debug::Status status = retrieved.error_value();
    DEBUG_LOG(Agent) << "Could not retrieve exception from limbo: " << status.message();
    return status;
  }

  LimboProvider::RetrievedException& exception = retrieved.value();

  DebuggedProcessCreateInfo create_info(std::move(exception.process));
  create_info.from_limbo = true;

  DebuggedProcess* debugged_process = nullptr;
  debug::Status status = AddDebuggedProcess(std::move(create_info), &debugged_process);
  if (status.has_error())
    return status;

  // Send the response, then the notifications about the process and threads.
  //
  // DO NOT RETURN FAILURE AFTER THIS POINT or the attach reply will be duplicated (the caller will
  // fall back on non-limbo processes if this function fails and will send another reply).
  SendAttachReply(this, transaction_id, debug::Status(), process_koid,
                  debugged_process->process_handle().GetName());

  debugged_process->PopulateCurrentThreads();
  debugged_process->SuspendAndSendModulesIfKnown();

  zx_koid_t thread_koid = exception.thread->GetKoid();

  // Pass in the exception handle to the corresponding thread.
  DebuggedThread* exception_thread = nullptr;
  for (DebuggedThread* thread : debugged_process->GetThreads()) {
    if (thread->koid() == thread_koid) {
      exception_thread = thread;
      break;
    }
  }

  if (exception_thread)
    exception_thread->set_exception_handle(std::move(exception.exception));

  return debug::Status();
}

debug::Status DebugAgent::AttachToExistingProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  std::unique_ptr<ProcessHandle> process_handle = system_interface_->GetProcess(process_koid);
  if (!process_handle)
    return debug::Status("Can not fild process " + std::to_string(process_koid) + " to attach to.");

  DebuggedProcess* process = nullptr;
  if (auto status =
          AddDebuggedProcess(DebuggedProcessCreateInfo(std::move(process_handle)), &process);
      status.has_error())
    return status;

  // Send the response, then the notifications about the process and threads.
  SendAttachReply(this, transaction_id, debug::Status(), process_koid,
                  process->process_handle().GetName());

  process->PopulateCurrentThreads();
  process->SuspendAndSendModulesIfKnown();

  return debug::Status();
}

void DebugAgent::LaunchProcess(const debug_ipc::LaunchRequest& request,
                               debug_ipc::LaunchReply* reply) {
  FX_DCHECK(!request.argv.empty());
  reply->inferior_type = debug_ipc::InferiorType::kBinary;
  DEBUG_LOG(Process) << "Launching binary " << request.argv.front();

  std::unique_ptr<BinaryLauncher> launcher = system_interface_->GetLauncher();
  reply->status = launcher->Setup(request.argv);
  if (reply->status.has_error())
    return;

  DebuggedProcessCreateInfo create_info(launcher->GetProcess());
  create_info.stdio = launcher->ReleaseStdioHandles();

  // The DebuggedProcess must be attached to the new process' exception port before actually
  // Starting the process to avoid racing with the program initialization.
  DebuggedProcess* new_process = nullptr;
  reply->status = AddDebuggedProcess(std::move(create_info), &new_process);
  if (reply->status.has_error())
    return;

  reply->status = launcher->Start();
  if (reply->status.has_error()) {
    RemoveDebuggedProcess(new_process->koid());
    return;
  }

  // Success, fill out the reply.
  reply->process_id = new_process->koid();
  reply->process_name = new_process->process_handle().GetName();
}

void DebugAgent::LaunchComponent(const debug_ipc::LaunchRequest& request,
                                 debug_ipc::LaunchReply* reply) {
  *reply = {};

  // This is a hack. It will fail if the debugger isn't already attached to either the system or
  // component root jobs. Ideally we would get the exact parent job for the component being launched
  // and not depend on what the client may have already attached to.
  DebuggedJob* job = GetDebuggedJob(attached_root_job_koid_);
  if (!job) {
    reply->status = debug::Status(
        "Could not obtain component root job. It could be another debugger is "
        "already running or this is a user OS build.");
    FX_LOGS(WARNING) << reply->status.message();
    return;
  }

  reply->inferior_type = debug_ipc::InferiorType::kComponent;
  reply->status = system_interface_->GetComponentManager().LaunchComponent(job, request.argv,
                                                                           &reply->component_id);
}

void DebugAgent::OnProcessStart(const std::string& filter,
                                std::unique_ptr<ProcessHandle> process_handle) {
  DEBUG_LOG(Process) << "Process starting, koid: " << process_handle->GetKoid();

  auto process_koid = process_handle->GetKoid();
  StdioHandles stdio;  // Will be filled in only for components.
  uint64_t component_id = system_interface_->GetComponentManager().OnProcessStart(filter, stdio);

  // Send notification, then create debug process so that thread notification is sent after this.
  debug_ipc::NotifyProcessStarting notify;
  notify.koid = process_koid;
  notify.name = process_handle->GetName();
  notify.component_id = component_id;
  notify.timestamp = GetNowTimestamp();

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessStarting(notify, &writer);
  stream()->Write(writer.MessageComplete());

  DebuggedProcessCreateInfo create_info(std::move(process_handle));
  create_info.stdio = std::move(stdio);

  DebuggedProcess* new_process = nullptr;
  AddDebuggedProcess(std::move(create_info), &new_process);

  if (new_process) {
    // In some edge-cases (see DebuggedProcess::RegisterDebugState() for more) the loader state is
    // known at startup. Send it if so.
    new_process->SuspendAndSendModulesIfKnown();
  }
}

void DebugAgent::InjectProcessForTest(std::unique_ptr<DebuggedProcess> process) {
  procs_[process->koid()] = std::move(process);
}

void DebugAgent::OnProcessEnteredLimbo(const LimboProvider::Record& record) {
  zx_koid_t process_koid = record.process->GetKoid();

  // First check if we were to "kill" this process.
  if (auto it = killed_limbo_procs_.find(process_koid); it != killed_limbo_procs_.end()) {
    system_interface_->GetLimboProvider().ReleaseProcess(process_koid);
    killed_limbo_procs_.erase(it);
    return;
  }

  std::string process_name = record.process->GetName();
  DEBUG_LOG(Agent) << "Process " << process_name << " (" << process_koid << ") entered limbo.";

  debug_ipc::NotifyProcessStarting process_starting = {};
  process_starting.type = debug_ipc::NotifyProcessStarting::Type::kLimbo;
  process_starting.koid = process_koid;
  process_starting.name = std::move(process_name);
  process_starting.timestamp = GetNowTimestamp();

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessStarting(std::move(process_starting), &writer);
  stream()->Write(writer.MessageComplete());
}

}  // namespace debug_agent
