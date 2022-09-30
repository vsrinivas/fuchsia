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
#include <zircon/types.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/binary_launcher.h"
#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/debug_agent/time.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol.h"
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
  // Register ourselves to receive component events and limbo events.
  //
  // It's safe to pass |this| here because |this| owns |system_interface|, which owns
  // |ComponentManager| and |LimboProvider|.
  system_interface_->GetComponentManager().SetDebugAgent(this);
  system_interface_->GetLimboProvider().set_on_enter_limbo(
      [this](const LimboProvider::Record& record) { OnProcessEnteredLimbo(record); });
}

fxl::WeakPtr<DebugAgent> DebugAgent::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void DebugAgent::Connect(debug::StreamBuffer* stream) {
  FX_DCHECK(!stream_) << "A debug agent should not be connected twice!";
  stream_ = stream;
  debug::LogBackend::Set(this, true);

  // Watch the root job.
  root_job_ = system_interface_->GetRootJob();
  auto status = root_job_->WatchJobExceptions(
      [this](std::unique_ptr<ProcessHandle> process) { OnProcessStart(std::move(process)); });
  if (status.has_error()) {
    LOGS(Error) << "Failed to watch the root job: " << status.message();
  }
}

void DebugAgent::Disconnect() {
  FX_DCHECK(stream_);
  stream_ = nullptr;
  debug::LogBackend::Unset();

  // Stop watching for process starting.
  root_job_.reset();
  // Removes breakpoints before we detach from the processes, although it should also be safe to
  // reverse the order.
  breakpoints_.clear();
  // Detach us from the processes.
  procs_.clear();
}

debug::StreamBuffer* DebugAgent::stream() {
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

Breakpoint* DebugAgent::GetBreakpoint(uint32_t breakpoint_id) {
  if (auto found = breakpoints_.find(breakpoint_id); found != breakpoints_.end())
    return &found->second;
  return nullptr;
}

void DebugAgent::RemoveBreakpoint(uint32_t breakpoint_id) {
  if (auto found = breakpoints_.find(breakpoint_id); found != breakpoints_.end())
    breakpoints_.erase(found);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request, debug_ipc::HelloReply* reply) {
  // Version and signature are default-initialized to their current values.
  reply->arch = arch::GetCurrentArch();
  reply->page_size = zx_system_get_page_size();
}

void DebugAgent::OnStatus(const debug_ipc::StatusRequest& request, debug_ipc::StatusReply* reply) {
  // Get the attached processes.
  reply->processes.reserve(procs_.size());
  for (auto& [process_koid, proc] : procs_) {
    debug_ipc::ProcessRecord process_record = {};
    process_record.process_koid = process_koid;
    process_record.process_name = proc->process_handle().GetName();
    process_record.component =
        system_interface_->GetComponentManager().FindComponentInfo(proc->process_handle());

    auto threads = proc->GetThreads();
    process_record.threads.reserve(threads.size());
    for (auto* thread : threads) {
      process_record.threads.emplace_back(
          thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
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
      process_record.component =
          system_interface_->GetComponentManager().FindComponentInfo(*record.process);

      // For now, only fill the thread blocked on exception.
      process_record.threads.push_back(record.thread->GetThreadRecord(process_koid));

      reply->limbo.push_back(std::move(process_record));
    }
  }
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request, debug_ipc::LaunchReply* reply) {
  reply->timestamp = GetNowTimestamp();
  if (request.argv.empty()) {
    reply->status = debug::Status("No launch arguments provided");
    return;
  }
  switch (request.inferior_type) {
    case debug_ipc::InferiorType::kBinary:
      LaunchProcess(request, reply);
      return;
    case debug_ipc::InferiorType::kComponent:
      reply->status = system_interface_->GetComponentManager().LaunchComponent(request.argv);
      return;
    case debug_ipc::InferiorType::kTest:
      reply->status = system_interface_->GetComponentManager().LaunchTest(
          request.argv[0], {request.argv.begin() + 1, request.argv.end()});
      return;
    case debug_ipc::InferiorType::kLast:
      reply->status = debug::Status("Invalid inferior type to launch.");
      return;
  }
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

  // First check if the process is waiting in limbo. If so, release it.
  LimboProvider& limbo = system_interface_->GetLimboProvider();
  if (limbo.Valid() && limbo.IsProcessInLimbo(request.koid)) {
    reply->status = limbo.ReleaseProcess(request.koid);
    return;
  }

  auto debug_process = GetDebuggedProcess(request.koid);
  if (debug_process) {
    RemoveDebuggedProcess(request.koid);
    reply->status = debug::Status();
  } else {
    reply->status = debug::Status("Not currently attached to process " +
                                  std::to_string(request.koid) + " to detach from.");
  }
}

void DebugAgent::OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply) {
  std::vector<debug_ipc::ProcessThreadId> paused;

  if (request.ids.empty()) {
    // Pause everything.
    paused = ClientSuspendAll();
  } else {
    // Pause specific threads.
    for (const debug_ipc::ProcessThreadId& id : request.ids) {
      if (DebuggedProcess* proc = GetDebuggedProcess(id.process)) {
        if (id.thread) {
          // Single thread in that process.
          if (DebuggedThread* thread = proc->GetThread(id.thread)) {
            thread->ClientSuspend(true);
            paused.push_back(id);
          } else {
            LOGS(Warn) << "Could not find thread by koid: " << id.thread;
          }
        } else {
          // All threads in the process.
          std::vector<debug_ipc::ProcessThreadId> proc_threads = proc->ClientSuspendAllThreads();
          paused.insert(paused.end(), proc_threads.begin(), proc_threads.end());
        }
      }
    }
  }

  // Save the affected thread info.
  for (const debug_ipc::ProcessThreadId& id : paused) {
    if (DebuggedThread* thread = GetDebuggedThread(id)) {
      reply->threads.push_back(
          thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
    }
  }
}

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
            LOGS(Warn) << "Could not find thread by koid: " << id.thread;
          }
        } else {
          // All threads in the process.
          proc->OnResume(request);
        }
      } else {
        LOGS(Warn) << "Could not find process by koid: " << id.process;
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
    LOGS(Error) << "Cannot find thread with koid: " << request.id.thread;
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
    LOGS(Error) << "Cannot find thread with koid: " << request.id.thread;
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
                                             const debug::AddressRange& range) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterWatchpoint(bp, range);

  // The process might legitimately be not found if there was a race between the process terminating
  // and a breakpoint add/change.
  return debug::Status("Process not found when adding watchpoint");
}

void DebugAgent::UnregisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                      const debug::AddressRange& range) {
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

void DebugAgent::OnUpdateFilter(const debug_ipc::UpdateFilterRequest& request,
                                debug_ipc::UpdateFilterReply* reply) {
  DEBUG_LOG(Agent) << "Received UpdateFilter request size=" << request.filters.size();
  filters_.clear();
  filters_.reserve(request.filters.size());
  for (const auto& filter : request.filters) {
    filters_.emplace_back(filter);
    reply->matched_processes = filters_.back().ApplyToJob(*root_job_, *system_interface_);
  }
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

void DebugAgent::OnSaveMinidump(const debug_ipc::SaveMinidumpRequest& request,
                                debug_ipc::SaveMinidumpReply* reply) {
  reply->status = debug::Status();

  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);

  if (!proc) {
    reply->status =
        debug::Status("No process found to save core from. Is there an attached process?");
    return;
  }

  proc->OnSaveMinidump(request, reply);
}

DebuggedProcess* DebugAgent::GetDebuggedProcess(zx_koid_t koid) {
  auto found = procs_.find(koid);
  if (found == procs_.end())
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
    std::vector<debug_ipc::ProcessThreadId> proc_threads;
    if (process_koid == except_process) {
      proc_threads = process->ClientSuspendAllThreads(except_thread);
    } else {
      proc_threads = process->ClientSuspendAllThreads();
    }

    affected.insert(affected.end(), proc_threads.begin(), proc_threads.end());
  }

  return affected;
}

debug::Status DebugAgent::AddDebuggedProcess(DebuggedProcessCreateInfo&& create_info,
                                             DebuggedProcess** new_process) {
  *new_process = nullptr;

  auto proc = std::make_unique<DebuggedProcess>(this);
  if (auto status = proc->Init(std::move(create_info)); status.has_error())
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

void DebugAgent::OnAttach(const debug_ipc::AttachRequest& request, debug_ipc::AttachReply* reply) {
  DEBUG_LOG(Agent) << "Attemping to attach to process " << request.koid;
  reply->timestamp = GetNowTimestamp();

  // See if we're already attached to this process.
  for (auto& [koid, proc] : procs_) {
    if (koid == request.koid) {
      reply->status = debug::Status(debug::Status::kAlreadyExists,
                                    "Already attached to process " + std::to_string(proc->koid()));
      DEBUG_LOG(Agent) << reply->status.message();
      return;
    }
  }

  // First check if the process is in limbo. Sends the appropiate replies/notifications.
  if (system_interface_->GetLimboProvider().Valid()) {
    reply->status = AttachToLimboProcess(request.koid, reply);
    if (reply->status.ok())
      return;

    DEBUG_LOG(Agent) << "Could not attach to process in limbo: " << reply->status.message();
  }

  // Attempt to attach to an existing process. Sends the appropriate replies/notifications.
  reply->status = AttachToExistingProcess(request.koid, reply);
  if (reply->status.ok())
    return;

  // We didn't find a process.
  DEBUG_LOG(Agent) << "Could not attach to existing process: " << reply->status.message();
}

debug::Status DebugAgent::AttachToLimboProcess(zx_koid_t process_koid,
                                               debug_ipc::AttachReply* reply) {
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

  DebuggedProcess* process = nullptr;
  debug::Status status = AddDebuggedProcess(std::move(create_info), &process);
  if (status.has_error())
    return status;

  reply->koid = process->koid();
  reply->name = process->process_handle().GetName();
  reply->component =
      system_interface_->GetComponentManager().FindComponentInfo(process->process_handle());

  // Send the reply first, then the notifications about the process and threads.
  debug::MessageLoop::Current()->PostTask(FROM_HERE, [weak_this = GetWeakPtr(), koid = reply->koid,
                                                      exception = std::move(exception)]() mutable {
    if (!weak_this)
      return;
    if (DebuggedProcess* process = weak_this->GetDebuggedProcess(koid)) {
      process->PopulateCurrentThreads();
      process->SuspendAndSendModulesIfKnown();

      zx_koid_t thread_koid = exception.thread->GetKoid();

      // Pass in the exception handle to the corresponding thread.
      DebuggedThread* exception_thread = nullptr;
      for (DebuggedThread* thread : process->GetThreads()) {
        if (thread->koid() == thread_koid) {
          exception_thread = thread;
          break;
        }
      }

      if (exception_thread)
        exception_thread->set_exception_handle(std::move(exception.exception));
    }
  });

  return debug::Status();
}

debug::Status DebugAgent::AttachToExistingProcess(zx_koid_t process_koid,
                                                  debug_ipc::AttachReply* reply) {
  std::unique_ptr<ProcessHandle> process_handle = system_interface_->GetProcess(process_koid);
  if (!process_handle)
    return debug::Status("Can't find process " + std::to_string(process_koid) + " to attach to.");

  DebuggedProcess* process = nullptr;
  if (auto status =
          AddDebuggedProcess(DebuggedProcessCreateInfo(std::move(process_handle)), &process);
      status.has_error())
    return status;

  reply->koid = process->koid();
  reply->name = process->process_handle().GetName();
  reply->component =
      system_interface_->GetComponentManager().FindComponentInfo(process->process_handle());

  // Send the reply first, then the notifications about the process and threads.
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [weak_this = GetWeakPtr(), koid = reply->koid]() mutable {
        if (!weak_this)
          return;
        if (DebuggedProcess* process = weak_this->GetDebuggedProcess(koid)) {
          process->PopulateCurrentThreads();
          process->SuspendAndSendModulesIfKnown();
        }
      });

  return debug::Status();
}

void DebugAgent::LaunchProcess(const debug_ipc::LaunchRequest& request,
                               debug_ipc::LaunchReply* reply) {
  FX_DCHECK(!request.argv.empty());
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

void DebugAgent::OnProcessStart(std::unique_ptr<ProcessHandle> process_handle) {
  if (procs_.find(process_handle->GetKoid()) != procs_.end()) {
    return;  // The process might have been attached in |LaunchProcess|.
  }

  debug_ipc::NotifyProcessStarting::Type type = debug_ipc::NotifyProcessStarting::Type::kLast;
  StdioHandles stdio;  // Will be filled in only for components.
  std::string process_name_override;

  if (system_interface_->GetComponentManager().OnProcessStart(*process_handle, &stdio,
                                                              &process_name_override)) {
    type = debug_ipc::NotifyProcessStarting::Type::kLaunch;
  } else if (std::any_of(filters_.begin(), filters_.end(), [&](const Filter& filter) {
               return filter.MatchesProcess(*process_handle, *system_interface_);
             })) {
    type = debug_ipc::NotifyProcessStarting::Type::kAttach;
  } else {
    return;
  }

  DEBUG_LOG(Process) << "Process starting, koid: " << process_handle->GetKoid();

  // Prepare the notification but don't send yet because |process_handle| will be moved and
  // |AddDebuggedProcess| may fail.
  debug_ipc::NotifyProcessStarting notify;
  notify.type = type;
  notify.koid = process_handle->GetKoid();
  notify.name = process_name_override.empty() ? process_handle->GetName() : process_name_override;
  notify.timestamp = GetNowTimestamp();
  notify.component = system_interface_->GetComponentManager().FindComponentInfo(*process_handle);

  DebuggedProcessCreateInfo create_info(std::move(process_handle));
  create_info.stdio = std::move(stdio);

  DebuggedProcess* new_process = nullptr;
  debug::Status status = AddDebuggedProcess(std::move(create_info), &new_process);

  if (status.has_error()) {
    LOGS(Warn) << "Failed to attach to process " << notify.koid << ": " << status.message();
    return;
  }

  stream()->Write(debug_ipc::SerializeNotifyProcessStarting(notify));

  // In some edge-cases (see DebuggedProcess::RegisterDebugState() for more) the loader state is
  // known at startup. Send it if so.
  new_process->SuspendAndSendModulesIfKnown();
}

void DebugAgent::OnComponentStarted(const std::string& moniker, const std::string& url) {
  if (std::any_of(filters_.begin(), filters_.end(),
                  [&](const Filter& f) { return f.MatchesComponent(moniker, url); })) {
    debug_ipc::NotifyComponent notify;
    notify.component.moniker = moniker;
    notify.component.url = url;
    notify.timestamp = GetNowTimestamp();

    stream()->Write(SerializeNotifyComponentStarting(notify));
  }
}

void DebugAgent::OnComponentExited(const std::string& moniker, const std::string& url) {
  if (std::any_of(filters_.begin(), filters_.end(),
                  [&](const Filter& f) { return f.MatchesComponent(moniker, url); })) {
    debug_ipc::NotifyComponent notify;
    notify.component.moniker = moniker;
    notify.component.url = url;
    notify.timestamp = GetNowTimestamp();

    stream()->Write(SerializeNotifyComponentExiting(notify));
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

  stream()->Write(debug_ipc::SerializeNotifyProcessStarting(process_starting));
}

void DebugAgent::WriteLog(debug::LogSeverity severity, const debug::FileLineFunction& location,
                          std::string log) {
  debug_ipc::NotifyLog notify;
  switch (severity) {
    case debug::LogSeverity::kInfo:
      return;  // Only forward warnings and errors for now.
    case debug::LogSeverity::kWarn:
      notify.severity = debug_ipc::NotifyLog::Severity::kWarn;
      break;
    case debug::LogSeverity::kError:
      notify.severity = debug_ipc::NotifyLog::Severity::kError;
      break;
  }
  notify.location.file = location.file();
  notify.location.function = location.function();
  notify.location.line = location.line();
  notify.log = log;

  stream()->Write(debug_ipc::SerializeNotifyLog(notify));
}

}  // namespace debug_agent
