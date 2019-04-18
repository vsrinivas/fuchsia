// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/termination_reason.h>
#include <zircon/features.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/binary_launcher.h"
#include "src/developer/debug/debug_agent/component_launcher.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/object_util.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/debug_agent/system_info.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/block_timer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

constexpr size_t kMegabyte = 1024 * 1024;

}  // namespace

DebugAgent::DebugAgent(debug_ipc::StreamBuffer* stream,
                       std::shared_ptr<sys::ServiceDirectory> services)
    : stream_(stream), services_(services), weak_factory_(this) {}

DebugAgent::~DebugAgent() = default;

fxl::WeakPtr<DebugAgent> DebugAgent::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
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

void DebugAgent::OnHello(const debug_ipc::HelloRequest& request,
                         debug_ipc::HelloReply* reply) {
  TIME_BLOCK();
  // Version and signature are default-initialized to their current values.
  reply->arch = arch::ArchProvider::Get().GetArch();
}

void DebugAgent::OnLaunch(const debug_ipc::LaunchRequest& request,
                          debug_ipc::LaunchReply* reply) {
  TIME_BLOCK();
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

void DebugAgent::OnKill(const debug_ipc::KillRequest& request,
                        debug_ipc::KillReply* reply) {
  TIME_BLOCK();
  auto debug_process = GetDebuggedProcess(request.process_koid);

  if (!debug_process || !debug_process->process().is_valid()) {
    reply->status = ZX_ERR_NOT_FOUND;
    return;
  }
  debug_process->OnKill(request, reply);
  RemoveDebuggedProcess(request.process_koid);
}

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

void DebugAgent::OnAttach(uint32_t transaction_id,
                          const debug_ipc::AttachRequest& request) {
  TIME_BLOCK();
  // Don't return early since we must send the reply at the bottom.
  debug_ipc::AttachReply reply;
  reply.status = ZX_ERR_NOT_FOUND;
  if (request.type == debug_ipc::TaskType::kProcess) {
    zx::process process = GetProcessFromKoid(request.koid);
    if (process.is_valid()) {
      reply.name = NameForObject(process);
      reply.koid = request.koid;

      // TODO(donosoc): change resume thread setting once we have global
      // settings.
      DebuggedProcessCreateInfo create_info;
      create_info.name = reply.name;
      create_info.koid = request.koid;
      create_info.handle = std::move(process);
      reply.status = AddDebuggedProcess(std::move(create_info));
    }

    // Send the reply.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteReply(reply, transaction_id, &writer);
    stream()->Write(writer.MessageComplete());

    // For valid attaches, follow up with the current module and thread lists.
    DebuggedProcess* new_process = GetDebuggedProcess(request.koid);
    if (new_process) {
      new_process->PopulateCurrentThreads();

      if (new_process->RegisterDebugState()) {
        // Suspend all threads while the module list is being sent. The client
        // will resume the threads once it's loaded symbols and processed
        // breakpoints (this may take a while and we'd like to get any
        // breakpoints as early as possible).
        std::vector<uint64_t> paused_thread_koids;
        new_process->SuspendAll(false, &paused_thread_koids);
        new_process->SendModuleNotification(std::move(paused_thread_koids));
      }
    }
  } else if (request.type == debug_ipc::TaskType::kJob) {
    zx::job job = GetJobFromKoid(request.koid);
    if (job.is_valid()) {
      reply.name = NameForObject(job);
      reply.koid = request.koid;
      reply.status = AddDebuggedJob(request.koid, std::move(job));
    }

    // Send the reply.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteReply(reply, transaction_id, &writer);
    stream()->Write(writer.MessageComplete());
  } else if (request.type == debug_ipc::TaskType::kComponentRoot) {
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
          reply.status = AddDebuggedJob(koid, std::move(job));
          if (reply.status == ZX_OK) {
            reply.status = ZX_OK;
            component_root_job_koid_ = koid;
          } else {
            FXL_LOG(ERROR) << "Could not attach to the root job: "
                           << debug_ipc::ZxStatusToString(reply.status);
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
  TIME_BLOCK();
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
  TIME_BLOCK();
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

void DebugAgent::OnResume(const debug_ipc::ResumeRequest& request,
                          debug_ipc::ResumeReply* reply) {
  TIME_BLOCK();
  if (request.process_koid) {
    // Single process.
    DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
    if (proc) {
      proc->OnResume(request);
    } else {
      FXL_LOG(WARNING) << "Could not find process by koid: "
                       << request.process_koid;
    }
  } else {
    // All debugged processes.
    for (const auto& pair : procs_)
      pair.second->OnResume(request);
  }
}

void DebugAgent::OnModules(const debug_ipc::ModulesRequest& request,
                           debug_ipc::ModulesReply* reply) {
  TIME_BLOCK();
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnModules(reply);
}

void DebugAgent::OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                               debug_ipc::ProcessTreeReply* reply) {
  TIME_BLOCK();
  GetProcessTree(&reply->root);
}

void DebugAgent::OnThreads(const debug_ipc::ThreadsRequest& request,
                           debug_ipc::ThreadsReply* reply) {
  TIME_BLOCK();
  auto found = procs_.find(request.process_koid);
  if (found == procs_.end())
    return;

  found->second->FillThreadRecords(&reply->threads);
}

void DebugAgent::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                              debug_ipc::ReadMemoryReply* reply) {
  TIME_BLOCK();
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnReadMemory(request, reply);
}

void DebugAgent::OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                                 debug_ipc::ReadRegistersReply* reply) {
  TIME_BLOCK();
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
  TIME_BLOCK();
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
  TIME_BLOCK();
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
  TIME_BLOCK();
  RemoveBreakpoint(request.breakpoint_id);
}

void DebugAgent::OnSysInfo(const debug_ipc::SysInfoRequest& request,
                           debug_ipc::SysInfoReply* reply) {
  char version[64];
  zx_system_get_version(version, sizeof(version));
  reply->version = version;

  reply->num_cpus = zx_system_get_num_cpus();
  reply->memory_mb = zx_system_get_physmem() / kMegabyte;

  zx_system_get_features(ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT,
                         &reply->hw_breakpoint_count);

  zx_system_get_features(ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT,
                         &reply->hw_watchpoint_count);
}

void DebugAgent::OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                                debug_ipc::ThreadStatusReply* reply) {
  TIME_BLOCK();
  DebuggedThread* thread =
      GetDebuggedThread(request.process_koid, request.thread_koid);
  if (thread) {
    thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kFull,
                             nullptr, &reply->record);
  } else {
    // When the thread is not found the thread record is set to "dead".
    reply->record.process_koid = request.process_koid;
    reply->record.thread_koid = request.thread_koid;
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
  TIME_BLOCK();
  DebuggedProcess* proc = GetDebuggedProcess(request.process_koid);
  if (proc)
    proc->OnAddressSpace(request, reply);
}

void DebugAgent::OnJobFilter(const debug_ipc::JobFilterRequest& request,
                             debug_ipc::JobFilterReply* reply) {
  TIME_BLOCK();
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
  TIME_BLOCK();
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

DebuggedThread* DebugAgent::GetDebuggedThread(zx_koid_t process_koid,
                                              zx_koid_t thread_koid) {
  DebuggedProcess* process = GetDebuggedProcess(process_koid);
  if (!process)
    return nullptr;
  return process->GetThread(thread_koid);
}

zx_status_t DebugAgent::AddDebuggedJob(zx_koid_t job_koid, zx::job zx_job) {
  auto job = std::make_unique<DebuggedJob>(this, job_koid, std::move(zx_job));
  zx_status_t status = job->Init();
  if (status != ZX_OK)
    return status;

  jobs_[job_koid] = std::move(job);
  return ZX_OK;
}

zx_status_t DebugAgent::AddDebuggedProcess(
    DebuggedProcessCreateInfo&& create_info) {
  zx_koid_t process_koid = create_info.koid;
  auto proc = std::make_unique<DebuggedProcess>(this, std::move(create_info));
  zx_status_t status = proc->Init();
  if (status != ZX_OK)
    return status;

  procs_[process_koid] = std::move(proc);
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
  zx_koid_t process_koid = KoidForObject(process);

  DebuggedProcessCreateInfo create_info;
  create_info.koid = process_koid;
  create_info.handle = std::move(process);
  create_info.out = launcher.ReleaseStdout();
  create_info.err = launcher.ReleaseStderr();
  zx_status_t status = AddDebuggedProcess(std::move(create_info));
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
  reply->process_name = NameForObject(process);
  reply->status = ZX_OK;
}

void DebugAgent::LaunchComponent(const debug_ipc::LaunchRequest& request,
                                 debug_ipc::LaunchReply* reply) {
  *reply = {};
  reply->inferior_type = debug_ipc::InferiorType::kComponent;

  ComponentLauncher component_launcher(services_);

  ComponentDescription description;
  ComponentHandles handles;
  zx_status_t status =
      component_launcher.Prepare(request.argv, &description, &handles);
  if (status != ZX_OK) {
    reply->status = status;
    return;
  }
  FXL_DCHECK(expected_components_.count(description.filter) == 0);

  // Create the filter
  DebuggedJob* job = GetDebuggedJob(component_root_job_koid_);
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
  controller.events().OnTerminated =
      [agent = GetWeakPtr(), description](
          int64_t return_code, fuchsia::sys::TerminationReason reason) {
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

void DebugAgent::OnProcessStart(const std::string& filter,
                                zx::process process_handle) {
  TIME_BLOCK();
  ComponentDescription description;
  ComponentHandles handles;
  auto it = expected_components_.find(filter);
  if (it != expected_components_.end()) {
    description = std::move(it->second.description);
    handles = std::move(it->second.handles);

    // Add to the list of running components.
    running_components_[description.component_id] =
        std::move(it->second.controller);
    expected_components_.erase(it);
  } else {
    description.process_name = NameForObject(process_handle);
  }

  auto process_koid = KoidForObject(process_handle);

  DEBUG_LOG(Process) << "Process starting. Name: " << description.process_name
                     << ", koid: " << process_koid << ", filter: " << filter
                     << ", component id: " << description.component_id;

  // Send notification, then create debug process so that thread notification is
  // sent after this.
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
  AddDebuggedProcess(std::move(create_info));
}

void DebugAgent::OnComponentTerminated(int64_t return_code,
                                       const ComponentDescription& description,
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
    ss << "Still expecting the following components: "
       << expected_components_.size();
    for (auto& expected : expected_components_) {
      ss << std::endl << "* " << expected.first;
    }
    DEBUG_LOG(Process) << ss.str();
  }
}

}  // namespace debug_agent
