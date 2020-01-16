// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/termination_reason.h>
#include <zircon/features.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_provider_impl.h"
#include "src/developer/debug/debug_agent/binary_launcher.h"
#include "src/developer/debug/debug_agent/component_launcher.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/debug_agent/system_info.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

// SystemProviders ---------------------------------------------------------------------------------

SystemProviders SystemProviders::CreateDefaults(std::shared_ptr<sys::ServiceDirectory> services) {
  SystemProviders system_providers;
  system_providers.arch_provider = std::make_shared<ArchProviderImpl>();
  system_providers.limbo_provider = std::make_shared<LimboProvider>(std::move(services));
  system_providers.object_provider = std::make_shared<ObjectProvider>();

  zx_status_t status = system_providers.limbo_provider->Init();
  if (status != ZX_OK)
    FXL_LOG(WARNING) << "Could not initialize limbo provider: " << zx_status_get_string(status);

  return system_providers;
}

// DebugAgent --------------------------------------------------------------------------------------

namespace {

constexpr size_t kMegabyte = 1024 * 1024;

std::string LogResumeRequest(const debug_ipc::ResumeRequest& request) {
  std::stringstream ss;
  ss << "Got resume request for process " << request.process_koid;

  // Print thread koids.
  if (!request.thread_koids.empty()) {
    ss << ", Threads: (";
    for (size_t i = 0; i < request.thread_koids.size(); i++) {
      if (i > 0)
        ss << ", ";
      ss << request.thread_koids[i];
    }
    ss << ")";
  }

  // Print step range.
  if (request.range_begin != request.range_end)
    ss << ", Range: [" << std::hex << request.range_begin << ", " << request.range_end << "]";

  return ss.str();
}

}  // namespace

DebugAgent::DebugAgent(std::shared_ptr<sys::ServiceDirectory> services, SystemProviders providers)
    : services_(services),
      arch_provider_(std::move(providers.arch_provider)),
      limbo_provider_(std::move(providers.limbo_provider)),
      object_provider_(std::move(providers.object_provider)),
      weak_factory_(this) {
  FXL_DCHECK(arch_provider_);
  FXL_DCHECK(limbo_provider_);
  FXL_DCHECK(object_provider_);

  // Get some resources.
  uint32_t val = 0;
  if (zx_status_t status = zx_system_get_features(ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT, &val);
      status == ZX_OK) {
    DEBUG_LOG(Agent) << "Got HW breakpoint count: " << val;
    arch_provider_->set_hw_breakpoint_count(val);
  } else {
    FXL_LOG(WARNING) << "Could not get HW breakpoint count: " << zx_status_get_string(status);
  }

  if (zx_status_t status = zx_system_get_features(ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT, &val);
      status == ZX_OK) {
    DEBUG_LOG(Agent) << "Got watchpoint count: " << val;
    arch_provider_->set_watchpoint_count(val);
  } else {
    FXL_LOG(WARNING) << "Could not get Watchpoint count: " << zx_status_get_string(status);
  }

  // Set a callback to the limbo_provider to let us know when new processes enter the limbo.
  limbo_provider_->set_on_enter_limbo(
      [agent = GetWeakPtr()](std::vector<fuchsia::exception::ProcessExceptionMetadata> processes) {
        if (!agent)
          return;
        agent->OnProcessesEnteredLimbo(std::move(processes));
      });
}

DebugAgent::~DebugAgent() = default;

fxl::WeakPtr<DebugAgent> DebugAgent::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void DebugAgent::Connect(debug_ipc::StreamBuffer* stream) {
  FXL_DCHECK(!stream_) << "A debug agent should not be connected twice!";
  stream_ = stream;
}

void DebugAgent::Disconnect() {
  FXL_DCHECK(stream_);
  stream_ = nullptr;
}

debug_ipc::StreamBuffer* DebugAgent::stream() {
  FXL_DCHECK(stream_);
  return stream_;
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

void DebugAgent::OnConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                               debug_ipc::ConfigAgentReply* reply) {
  reply->results = HandleActions(request.actions, &configuration_);
}

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request, debug_ipc::HelloReply* reply) {
  // Version and signature are default-initialized to their current values.
  reply->arch = arch_provider_->GetArch();
}

void DebugAgent::OnStatus(const debug_ipc::StatusRequest& request, debug_ipc::StatusReply* reply) {
  // Get the attached processes.
  reply->processes.reserve(procs_.size());
  for (auto& [process_koid, proc] : procs_) {
    debug_ipc::ProcessRecord process_record = {};
    process_record.process_koid = process_koid;
    process_record.process_name = proc->name();

    auto threads = proc->GetThreads();
    process_record.threads.reserve(threads.size());
    for (auto* thread : threads) {
      debug_ipc::ThreadRecord thread_record;
      thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kNone, nullptr,
                               &thread_record);
      process_record.threads.emplace_back(std::move(thread_record));
    }

    reply->processes.emplace_back(std::move(process_record));
  }

  // Get the limbo processes.
  if (limbo_provider_->Valid()) {
    std::vector<fuchsia::exception::ProcessExceptionMetadata> limbo_processes;
    for (auto& [process_koid, process_metadata] : limbo_provider_->Limbo()) {
      debug_ipc::ProcessRecord process_record = {};
      process_record.process_koid = process_metadata.info().process_koid;
      process_record.process_name = object_provider_->NameForObject(process_metadata.process());

      // For now, only fill the thread on exception.
      debug_ipc::ThreadRecord thread_record = {};
      thread_record.process_koid = process_record.process_koid;
      thread_record.thread_koid = process_metadata.info().thread_koid;
      thread_record.name = object_provider_->NameForObject(process_metadata.thread());
      thread_record.state = debug_ipc::ThreadRecord::State::kBlocked;
      thread_record.blocked_reason = debug_ipc::ThreadRecord::BlockedReason::kException;

      process_record.threads.push_back(std::move(thread_record));

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

  reply->status = ZX_ERR_INVALID_ARGS;
}

void DebugAgent::OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply) {
  // See first if the process is on limbo.
  auto limbo_it = limbo_provider_->Limbo().find(request.process_koid);
  if (limbo_it != limbo_provider_->Limbo().end()) {
    // Release if from limbo, which will effectivelly kill it.
    reply->status = limbo_provider_->ReleaseProcess(request.process_koid);
    return;
  }

  // Otherwise we search locally.
  auto debug_process = GetDebuggedProcess(request.process_koid);
  if (!debug_process || !debug_process->handle().is_valid()) {
    reply->status = ZX_ERR_NOT_FOUND;
    return;
  }

  debug_process->OnKill(request, reply);
  RemoveDebuggedProcess(request.process_koid);
}

void DebugAgent::OnDetach(const debug_ipc::DetachRequest& request, debug_ipc::DetachReply* reply) {
  switch (request.type) {
    case debug_ipc::TaskType::kJob: {
      auto debug_job = GetDebuggedJob(request.koid);
      if (debug_job && debug_job->job().is_valid()) {
        RemoveDebuggedJob(request.koid);
        reply->status = ZX_OK;
      } else {
        reply->status = ZX_ERR_NOT_FOUND;
      }
      break;
    }
    case debug_ipc::TaskType::kProcess: {
      // First check if the process is waiting in limbo. If so, we release it.
      auto limbo_it = limbo_provider_->Limbo().find(request.koid);
      if (limbo_it != limbo_provider_->Limbo().end()) {
        reply->status = limbo_provider_->ReleaseProcess(request.koid);
        return;
      }

      auto debug_process = GetDebuggedProcess(request.koid);
      if (debug_process && debug_process->handle().is_valid()) {
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

void DebugAgent::OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply) {
  if (request.process_koid) {
    // Single process.
    DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
    if (proc)
      proc->OnPause(request, reply);
  } else {
    // All debugged processes.
    for (const auto& pair : procs_)
      pair.second->OnPause(request, reply);
  }
}

void DebugAgent::OnQuitAgent(const debug_ipc::QuitAgentRequest& request,
                             debug_ipc::QuitAgentReply* reply) {
  debug_ipc::MessageLoop::Current()->QuitNow();
};

void DebugAgent::OnResume(const debug_ipc::ResumeRequest& request, debug_ipc::ResumeReply* reply) {
  DEBUG_LOG(Agent) << LogResumeRequest(request);

  if (request.process_koid) {
    // Single process.
    DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
    if (proc) {
      proc->OnResume(request);
    } else {
      FXL_LOG(WARNING) << "Could not find process by koid: " << request.process_koid;
    }
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
  GetProcessTree(&reply->root, *object_provider_);
}

void DebugAgent::OnThreads(const debug_ipc::ThreadsRequest& request,
                           debug_ipc::ThreadsReply* reply) {
  auto found = procs_.find(request.process_koid);
  if (found == procs_.end())
    return;

  found->second->FillThreadRecords(&reply->threads);
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnReadMemory(request, reply);
}

void DebugAgent::OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                                 debug_ipc::ReadRegistersReply* reply) {
  DebuggedThread* thread = GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    thread->ReadRegisters(request.categories, &reply->registers);
  } else {
    FXL_LOG(ERROR) << "Cannot find thread with koid: " << request.thread_koid;
  }
}

void DebugAgent::OnWriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                                  debug_ipc::WriteRegistersReply* reply) {
  DebuggedThread* thread = GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    reply->status = thread->WriteRegisters(request.registers, &reply->registers);
  } else {
    reply->status = ZX_ERR_NOT_FOUND;
    FXL_LOG(ERROR) << "Cannot find thread with koid: " << request.thread_koid;
  }
}

void DebugAgent::OnAddOrChangeBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                                         debug_ipc::AddOrChangeBreakpointReply* reply) {
  switch (request.breakpoint_type) {
    case debug_ipc::BreakpointType::kSoftware:
    case debug_ipc::BreakpointType::kHardware:
    case debug_ipc::BreakpointType::kReadWrite:
    case debug_ipc::BreakpointType::kWrite:
      return SetupBreakpoint(request, reply);
    case debug_ipc::BreakpointType::kLast:
      break;
  }

  FXL_NOTREACHED() << "Invalid Breakpoint Type: " << static_cast<int>(request.breakpoint_type);
}

void DebugAgent::OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                                    debug_ipc::RemoveBreakpointReply* reply) {
  RemoveBreakpoint(request.breakpoint_id);
}

void DebugAgent::OnSysInfo(const debug_ipc::SysInfoRequest& request,
                           debug_ipc::SysInfoReply* reply) {
  char version[64];
  zx_system_get_version(version, sizeof(version));
  reply->version = version;

  reply->num_cpus = zx_system_get_num_cpus();
  reply->memory_mb = zx_system_get_physmem() / kMegabyte;

  reply->hw_watchpoint_count = arch_provider_->hw_breakpoint_count();
  reply->hw_watchpoint_count = arch_provider_->watchpoint_count();
}

void DebugAgent::OnProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                                 debug_ipc::ProcessStatusReply* reply) {
  auto it = procs_.find(request.process_koid);
  if (it == procs_.end()) {
    reply->status = ZX_ERR_NOT_FOUND;
    return;
  }

  DebuggedProcess* process = it->second.get();

  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [this, process]() mutable {
    debug_ipc::NotifyProcessStarting notify = {};
    notify.koid = process->koid();
    notify.name = process->name();

    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyProcessStarting(notify, &writer);
    stream()->Write(writer.MessageComplete());

    // Send the modules notification.
    process->SuspendAndSendModulesIfKnown();
  });

  reply->status = ZX_OK;
}

void DebugAgent::OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                                debug_ipc::ThreadStatusReply* reply) {
  DebuggedThread* thread = GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kFull, nullptr, &reply->record);
  } else {
    // When the thread is not found the thread record is set to "dead".
    reply->record.process_koid = request.process_koid;
    reply->record.thread_koid = request.thread_koid;
    reply->record.state = debug_ipc::ThreadRecord::State::kDead;
  }
}

zx_status_t DebugAgent::RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                           uint64_t address) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterBreakpoint(bp, address);

  // The process might legitimately be not found if there was a race between
  // the process terminating and a breakpoint add/change.
  return ZX_ERR_NOT_FOUND;
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

  reply->status = found->second.SetSettings(request.breakpoint_type, request.breakpoint);
}

zx_status_t DebugAgent::RegisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                           const debug_ipc::AddressRange& range) {
  DebuggedProcess* proc = GetDebuggedProcess(process_koid);
  if (proc)
    return proc->RegisterWatchpoint(bp, range);

  // The process might legitimately be not found if there was a race between
  // the process terminating and a breakpoint add/change.
  return ZX_ERR_NOT_FOUND;
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
    reply->status = ZX_ERR_INVALID_ARGS;
    return;
  }

  auto matches = job->SetFilters(std::move(request.filters));
  for (zx_koid_t process_koid : matches) {
    reply->matched_processes.push_back(process_koid);
  }

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

DebuggedThread* DebugAgent::GetDebuggedThread(zx_koid_t process_koid, zx_koid_t thread_koid) {
  DebuggedProcess* process = GetDebuggedProcess(process_koid);
  if (!process)
    return nullptr;
  return process->GetThread(thread_koid);
}

zx_status_t DebugAgent::AddDebuggedJob(zx_koid_t job_koid, zx::job zx_job) {
  auto job = std::make_unique<DebuggedJob>(object_provider_, this, job_koid, std::move(zx_job));
  zx_status_t status = job->Init();
  if (status != ZX_OK)
    return status;

  jobs_[job_koid] = std::move(job);
  return ZX_OK;
}

zx_status_t DebugAgent::AddDebuggedProcess(DebuggedProcessCreateInfo&& create_info,
                                           DebuggedProcess** new_process) {
  *new_process = nullptr;

  zx_koid_t process_koid = create_info.koid;
  auto proc = std::make_unique<DebuggedProcess>(this, std::move(create_info));
  if (zx_status_t status = proc->Init(); status != ZX_OK)
    return status;

  *new_process = proc.get();
  procs_[process_koid] = std::move(proc);
  return ZX_OK;
}

// Attaching ---------------------------------------------------------------------------------------

namespace {

void SendAttachReply(DebugAgent* debug_agent, uint32_t transaction_id, zx_status_t status,
                     zx_koid_t process_koid = ZX_HANDLE_INVALID,
                     const std::string& process_name = "") {
  debug_ipc::AttachReply reply = {};
  reply.status = status;
  reply.koid = process_koid;
  reply.name = process_name;

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
    FXL_LOG(WARNING) << "Got bad debugger attach request, ignoring.";
    return;
  }

  OnAttach(transaction_id, request);
}

void DebugAgent::OnAttach(uint32_t transaction_id, const debug_ipc::AttachRequest& request) {
  if (request.type == debug_ipc::TaskType::kProcess) {
    AttachToProcess(request.koid, transaction_id);
    return;
  }

  // All other attach types are variants of job attaches, find the KOID.
  zx_koid_t job_koid = 0;
  if (request.type == debug_ipc::TaskType::kJob) {
    job_koid = request.koid;
  } else if (request.type == debug_ipc::TaskType::kComponentRoot) {
    job_koid = object_provider_->GetComponentJobKoid();
    attached_root_job_koid_ = job_koid;
  } else if (request.type == debug_ipc::TaskType::kSystemRoot) {
    job_koid = object_provider_->GetRootJobKoid();
    attached_root_job_koid_ = job_koid;
  } else {
    FXL_LOG(WARNING) << "Got bad debugger attach request type, ignoring.";
    return;
  }

  debug_ipc::AttachReply reply;
  reply.status = ZX_ERR_NOT_FOUND;

  // Don't return early since we always need to send the reply, even on fail.
  zx::job job = object_provider_->GetJobFromKoid(job_koid);
  if (job.is_valid()) {
    reply.name = object_provider_->NameForObject(job);
    reply.koid = job_koid;
    reply.status = AddDebuggedJob(job_koid, std::move(job));
  }

  DEBUG_LOG(Agent) << "Attaching to job " << job_koid << ": " << zx_status_get_string(reply.status);

  // Send the reply.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteReply(reply, transaction_id, &writer);
  stream()->Write(writer.MessageComplete());
}

void DebugAgent::AttachToProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  DEBUG_LOG(Agent) << "Attemping to attach to process " << process_koid;
  // We see if we're already attached to this process.
  for (auto& [koid, proc] : procs_) {
    if (koid == process_koid) {
      DEBUG_LOG(Agent) << "Already attached to " << proc->name() << "(" << proc->koid() << ").";
      SendAttachReply(this, transaction_id, ZX_ERR_ALREADY_BOUND);
      return;
    }
  }

  // First check if the process is in limbo. Sends the appropiate replies/notifications.
  if (limbo_provider_->Valid()) {
    zx_status_t status = AttachToLimboProcess(process_koid, transaction_id);
    if (status == ZX_OK)
      return;

    DEBUG_LOG(Agent) << "Could not attach to process in limbo: " << zx_status_get_string(status);
  }

  // Attempt to attach to an existing process. Sends the appropiate replies/notifications.
  zx_status_t status = AttachToExistingProcess(process_koid, transaction_id);
  if (status == ZX_OK)
    return;

  DEBUG_LOG(Agent) << "Could not attach to existing process: " << zx_status_get_string(status);

  // We didn't find a process.
  SendAttachReply(this, transaction_id, status);
}

zx_status_t DebugAgent::AttachToLimboProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  FXL_DCHECK(limbo_provider_->Valid());

  // Go over processes.
  const fuchsia::exception::ProcessExceptionMetadata* process_metadata = nullptr;
  for (const auto& [process_koid, exception] : limbo_provider_->Limbo()) {
    FXL_DCHECK(exception.has_info());
    DEBUG_LOG(Agent) << "Found process in limbo: " << exception.info().process_koid;
    if (exception.info().process_koid == process_koid) {
      process_metadata = &exception;
      break;
    }
  }

  if (!process_metadata)
    return ZX_ERR_NOT_FOUND;

  // Obtain the process from limbo.
  fuchsia::exception::ProcessException exception;
  zx_status_t status = limbo_provider_->RetrieveException(process_koid, &exception);
  if (status != ZX_OK) {
    DEBUG_LOG(Agent) << "Could not retrieve exception from limbo: " << zx_status_get_string(status);
    return status;
  }

  // We attach to the process.
  DebuggedProcessCreateInfo create_info = {};
  create_info.koid = process_koid;
  create_info.name = object_provider_->NameForObject(exception.process());
  create_info.handle = std::move(*exception.mutable_process());
  create_info.arch_provider = arch_provider_;
  create_info.object_provider = object_provider_;

  DebuggedProcess* process = nullptr;
  status = AddDebuggedProcess(std::move(create_info), &process);
  if (status != ZX_OK)
    return status;

  // Send the response, then the notifications about the process and threads.
  SendAttachReply(this, transaction_id, ZX_OK, process_koid, process->name());

  process->PopulateCurrentThreads();
  process->SuspendAndSendModulesIfKnown();

  // Pass in the exception handle to the corresponding thread.
  DebuggedThread* exception_thread = nullptr;
  for (DebuggedThread* thread : process->GetThreads()) {
    if (thread->koid() == exception.info().thread_koid) {
      exception_thread = thread;
      break;
    }
  }

  FXL_DCHECK(exception_thread);
  exception_thread->set_exception_handle(std::move(*exception.mutable_exception()));

  return ZX_OK;
}

zx_status_t DebugAgent::AttachToExistingProcess(zx_koid_t process_koid, uint32_t transaction_id) {
  zx::process process_handle = object_provider_->GetProcessFromKoid(process_koid);
  if (!process_handle.is_valid())
    return ZX_ERR_NOT_FOUND;

  // We attach to the process.
  DebuggedProcessCreateInfo create_info = {};
  create_info.koid = process_koid;
  create_info.name = object_provider_->NameForObject(process_handle);
  create_info.handle = std::move(process_handle);
  create_info.arch_provider = arch_provider_;
  create_info.object_provider = object_provider_;

  DebuggedProcess* process = nullptr;
  zx_status_t status = AddDebuggedProcess(std::move(create_info), &process);
  if (status != ZX_OK)
    return status;

  // Send the response, then the notifications about the process and threads.
  SendAttachReply(this, transaction_id, ZX_OK, process_koid, process->name());

  process->PopulateCurrentThreads();
  process->SuspendAndSendModulesIfKnown();

  return ZX_OK;
}

void DebugAgent::LaunchProcess(const debug_ipc::LaunchRequest& request,
                               debug_ipc::LaunchReply* reply) {
  FXL_DCHECK(!request.argv.empty());
  reply->inferior_type = debug_ipc::InferiorType::kBinary;
  DEBUG_LOG(Process) << "Launching binary " << request.argv.front();

  BinaryLauncher launcher(services_);

  reply->status = launcher.Setup(request.argv);
  if (reply->status != ZX_OK)
    return;

  zx::process process = launcher.GetProcess();
  zx_koid_t process_koid = object_provider_->KoidForObject(process);

  DebuggedProcessCreateInfo create_info;
  create_info.koid = process_koid;
  create_info.handle = std::move(process);
  create_info.out = launcher.ReleaseStdout();
  create_info.err = launcher.ReleaseStderr();
  create_info.arch_provider = arch_provider_;
  create_info.object_provider = object_provider_;

  DebuggedProcess* new_process = nullptr;
  zx_status_t status = AddDebuggedProcess(std::move(create_info), &new_process);
  if (status != ZX_OK) {
    reply->status = status;
    return;
  }

  reply->status = launcher.Start();
  if (reply->status != ZX_OK) {
    RemoveDebuggedProcess(process_koid);
    return;
  }

  // Success, fill out the reply.
  reply->process_id = process_koid;
  reply->process_name = object_provider_->NameForObject(process);
  reply->status = ZX_OK;
}

void DebugAgent::LaunchComponent(const debug_ipc::LaunchRequest& request,
                                 debug_ipc::LaunchReply* reply) {
  *reply = {};
  reply->inferior_type = debug_ipc::InferiorType::kComponent;

  ComponentLauncher component_launcher(services_);

  ComponentDescription description;
  ComponentHandles handles;
  zx_status_t status = component_launcher.Prepare(request.argv, &description, &handles);
  if (status != ZX_OK) {
    reply->status = status;
    return;
  }
  FXL_DCHECK(expected_components_.count(description.filter) == 0);

  // Create the filter.
  //
  // This is a hack. It will fail if the debugger isn't already attached to
  // either the system or component root jobs. Ideally we would get the exact
  // parent job for the component being launched and not depend on what the
  // client may have already attached to.
  DebuggedJob* job = GetDebuggedJob(attached_root_job_koid_);
  if (!job) {
    FXL_LOG(WARNING) << "Could not obtain component root job. Are you running "
                        "attached to another debugger?";
    reply->status = ZX_ERR_BAD_STATE;
    return;
  }
  job->AppendFilter(description.filter);

  if (debug_ipc::IsDebugModeActive()) {
    std::stringstream ss;

    ss << "Launching component. " << std::endl
       << "Url: " << description.url << std::endl
       << ", name: " << description.process_name << std::endl
       << ", filter: " << description.filter << std::endl
       << ", component_id: " << description.component_id << std::endl;

    auto& filters = job->filters();
    ss << "Current component filters: " << filters.size();
    for (auto& filter : filters) {
      ss << std::endl << "* " << filter.filter;
    }

    DEBUG_LOG(Process) << ss.str();
  }

  reply->component_id = description.component_id;

  // Launch the component.
  auto controller = component_launcher.Launch();
  if (!controller) {
    FXL_LOG(WARNING) << "Could not launch component " << description.url;
    reply->status = ZX_ERR_BAD_STATE;
    return;
  }

  // TODO(donosoc): This should hook into the debug agent so it can correctly
  //                shutdown the state associated with waiting for this
  //                component.
  controller.events().OnTerminated = [agent = GetWeakPtr(), description](
                                         int64_t return_code,
                                         fuchsia::sys::TerminationReason reason) {
    // If the agent is gone, there isn't anything more to do.
    if (!agent)
      return;

    agent->OnComponentTerminated(return_code, description, reason);
  };

  ExpectedComponent expected_component;
  expected_component.description = description;
  expected_component.handles = std::move(handles);
  expected_component.controller = std::move(controller);
  expected_components_[description.filter] = std::move(expected_component);

  reply->status = ZX_OK;
}

void DebugAgent::OnProcessStart(const std::string& filter, zx::process process_handle) {
  ComponentDescription description;
  ComponentHandles handles;
  auto it = expected_components_.find(filter);
  if (it != expected_components_.end()) {
    description = std::move(it->second.description);
    handles = std::move(it->second.handles);

    // Add to the list of running components.
    running_components_[description.component_id] = std::move(it->second.controller);
    expected_components_.erase(it);
  } else {
    description.process_name = object_provider_->NameForObject(process_handle);
  }

  auto process_koid = object_provider_->KoidForObject(process_handle);

  DEBUG_LOG(Process) << "Process starting. Name: " << description.process_name
                     << ", koid: " << process_koid << ", filter: " << filter
                     << ", component id: " << description.component_id;

  // Send notification, then create debug process so that thread notification is sent after this.
  debug_ipc::NotifyProcessStarting notify;
  notify.koid = process_koid;
  notify.name = description.process_name;
  notify.component_id = description.component_id;
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessStarting(notify, &writer);
  stream()->Write(writer.MessageComplete());

  DebuggedProcessCreateInfo create_info;
  create_info.koid = process_koid;
  create_info.handle = std::move(process_handle);
  create_info.name = description.process_name;
  create_info.out = std::move(handles.out);
  create_info.err = std::move(handles.err);
  create_info.arch_provider = arch_provider_;
  create_info.object_provider = object_provider_;

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

void DebugAgent::OnComponentTerminated(int64_t return_code, const ComponentDescription& description,
                                       fuchsia::sys::TerminationReason reason) {
  DEBUG_LOG(Process) << "Component " << description.url << " exited with "
                     << sys::HumanReadableTerminationReason(reason);

  // TODO(donosoc): This need to be communicated over to the client.
  if (reason != fuchsia::sys::TerminationReason::EXITED) {
    FXL_LOG(WARNING) << "Component " << description.url << " exited with "
                     << sys::HumanReadableTerminationReason(reason);
  }

  // We look for the filter and remove it.
  // If we couldn't find it, the component was already caught and cleaned.
  expected_components_.erase(description.filter);

  if (debug_ipc::IsDebugModeActive()) {
    std::stringstream ss;
    ss << "Still expecting the following components: " << expected_components_.size();
    for (auto& expected : expected_components_) {
      ss << std::endl << "* " << expected.first;
    }
    DEBUG_LOG(Process) << ss.str();
  }
}

void DebugAgent::OnProcessesEnteredLimbo(
    std::vector<fuchsia::exception::ProcessExceptionMetadata> processes) {
  for (auto& process : processes) {
    DEBUG_LOG(Agent) << "Process " << process.info().process_koid << " entered limbo.";

    debug_ipc::NotifyProcessStarting process_starting = {};
    process_starting.type = debug_ipc::NotifyProcessStarting::Type::kLimbo;
    process_starting.koid = process.info().process_koid;
    process_starting.name = object_provider_->NameForObject(process.process());

    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyProcessStarting(std::move(process_starting), &writer);
    stream()->Write(writer.MessageComplete());
  }
}

}  // namespace debug_agent
