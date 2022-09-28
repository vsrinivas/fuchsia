// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_process.h"

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/exception.h>

#include <utility>

#include "src/developer/debug/debug_agent/align.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/elf_utils.h"
#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/debug_agent/thread_handle.h"
#include "src/developer/debug/debug_agent/time.h"
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

std::vector<char> ReadSocketInput(BufferedStdioHandle* buffer) {
  FX_DCHECK(buffer->IsValid());

  constexpr size_t kReadSize = 1024;  // Read in 1K chunks.

  std::vector<char> data;
  auto& stream = buffer->stream();
  while (true) {
    char buf[kReadSize];

    size_t read_amount = stream.Read(buf, kReadSize);
    data.insert(data.end(), buf, buf + read_amount);

    if (read_amount < kReadSize)
      break;
  }
  return data;
}

// Meant to be used in debug logging.
std::string LogPreamble(const DebuggedProcess* process) {
  return fxl::StringPrintf("[P: %lu (%s)] ", process->koid(),
                           process->process_handle().GetName().c_str());
}

void LogRegisterBreakpoint(debug::FileLineFunction location, DebuggedProcess* process,
                           Breakpoint* bp, uint64_t address) {
  if (!debug::IsDebugLoggingActive())
    return;

  std::stringstream ss;
  ss << LogPreamble(process) << "Setting breakpoint " << bp->settings().id << " ("
     << bp->settings().name << ") on 0x" << std::hex << address;

  if (bp->settings().one_shot)
    ss << " (one shot)";

  DEBUG_LOG_WITH_LOCATION(Process, location) << ss.str();
}

}  // namespace

// DebuggedProcessCreateInfo -----------------------------------------------------------------------

DebuggedProcessCreateInfo::DebuggedProcessCreateInfo(std::unique_ptr<ProcessHandle> handle)
    : handle(std::move(handle)) {}

// DebuggedProcess ---------------------------------------------------------------------------------

DebuggedProcess::DebuggedProcess(DebugAgent* debug_agent) : debug_agent_(debug_agent) {}

DebuggedProcess::~DebuggedProcess() {
  if (process_handle_) {
    DetachFromProcess();
  }
}

void DebuggedProcess::DetachFromProcess() {
  DEBUG_LOG(Process) << LogPreamble(this) << "DetachFromProcess";

  // 1. Remove installed software breakpoints. We need to tell each thread that this will happen.
  for (auto& [address, breakpoint] : software_breakpoints_) {
    for (auto& [thread_koid, thread] : threads_) {
      thread->WillDeleteProcessBreakpoint(breakpoint.get());
    }
  }

  // Clear the resources.
  software_breakpoints_.clear();
  hardware_breakpoints_.clear();
  watchpoints_.clear();

  // 2. Resume threads. Technically a 0'ed request would work, but being explicit is future-proof.
  debug_ipc::ResumeRequest resume_request = {};
  resume_request.how = debug_ipc::ResumeRequest::How::kResolveAndContinue;
  resume_request.ids.push_back({.process = koid(), .thread = 0});
  OnResume(resume_request);

  // 3. Unbind from notifications (this will detach from the process).
  process_handle_->Detach();
}

debug::Status DebuggedProcess::Init(DebuggedProcessCreateInfo create_info) {
  // Watch for process events.
  if (debug::Status status = create_info.handle->Attach(this); status.has_error())
    return status;

  process_handle_ = std::move(create_info.handle);
  from_limbo_ = create_info.from_limbo;

  if (create_info.stdio.out.is_valid())
    SetStdout(std::move(create_info.stdio.out));
  if (create_info.stdio.err.is_valid())
    SetStderr(std::move(create_info.stdio.err));

  // Update module list.
  module_list_.Update(process_handle());

  return debug::Status();
}

void DebuggedProcess::OnResume(const debug_ipc::ResumeRequest& request) {
  if (request.ids.empty()) {
    // Empty thread ID list means resume all threads.
    for (auto& [thread_koid, thread] : threads_)
      thread->ClientResume(request);
  } else {
    for (const debug_ipc::ProcessThreadId& id : request.ids) {
      if (id.process != koid()) {
        // The request may contain resume requests for more than one process.
        continue;
      }
      if (!id.thread) {
        // A 0 thread koid will resume all threads of the given process.
        for (auto& [thread_koid, thread] : threads_) {
          thread->ClientResume(request);
        }
      } else if (DebuggedThread* thread = GetThread(id.thread)) {
        // Might be not found if there is a race between the thread exiting and the client sending
        // the request.
        thread->ClientResume(request);
      }
    }
  }
}

void DebuggedProcess::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                                   debug_ipc::ReadMemoryReply* reply) {
  reply->blocks = process_handle_->ReadMemoryBlocks(request.address, request.size);

  // Remove any breakpoint instructions we've inserted.
  //
  // If there are a lot of ProcessBreakpoints this will get slow. If we find we have 100's of
  // breakpoints an auxiliary data structure could be added to find overlapping breakpoints faster.
  for (const auto& [addr, bp] : software_breakpoints_) {
    // Generally there will be only one block. If we start reading many megabytes that cross
    // mapped memory boundaries, a top-level range check would be a good idea to avoid unnecessary
    // iteration.
    for (auto& block : reply->blocks) {
      bp->FixupMemoryBlock(&block);
    }
  }
}

void DebuggedProcess::OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply) {
  // Stop observing before killing the process to avoid getting exceptions after we stopped
  // listening to them.
  process_handle_->Detach();

  // Since we're being killed, we treat this process as not having any more
  // threads. This makes cleanup code more straightforward, as there are no
  // threads to resume/handle.
  threads_.clear();

  reply->status = process_handle_->Kill();
}

void DebuggedProcess::OnSaveMinidump(const debug_ipc::SaveMinidumpRequest& request,
                                     debug_ipc::SaveMinidumpReply* reply) {
  reply->status = process_handle_->SaveMinidump(GetThreads(), &reply->core_data);
}

DebuggedThread* DebuggedProcess::GetThread(zx_koid_t thread_koid) const {
  auto found_thread = threads_.find(thread_koid);
  if (found_thread == threads_.end())
    return nullptr;
  return found_thread->second.get();
}

std::vector<DebuggedThread*> DebuggedProcess::GetThreads() const {
  std::vector<DebuggedThread*> threads;
  threads.reserve(threads_.size());
  for (auto& kv : threads_)
    threads.emplace_back(kv.second.get());
  return threads;
}

void DebuggedProcess::PopulateCurrentThreads() {
  for (auto& thread : process_handle_->GetChildThreads()) {
    // We should never populate the same thread twice.
    zx_koid_t thread_koid = thread->GetKoid();
    if (threads_.find(thread_koid) != threads_.end())
      continue;

    auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, this, std::move(thread));
    threads_.emplace(thread_koid, std::move(new_thread));
  }
}

std::vector<debug_ipc::ThreadRecord> DebuggedProcess::GetThreadRecords() const {
  std::vector<debug_ipc::ThreadRecord> result;
  for (const auto& pair : threads_)
    result.push_back(pair.second->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
  return result;
}

bool DebuggedProcess::HandleLoaderBreakpoint(uint64_t address) {
  // The loader breakpoint is a hardcodeed breakpoint with a known address.
  if (address != process_handle().GetLoaderBreakpointAddress())
    return false;

  if (module_list_.Update(process_handle())) {
    // The debugged process could be multithreaded and have just dynamically loaded a new
    // module. Suspend all threads so the client can resolve breakpoint addresses before
    // continuing.
    SuspendAndSendModulesIfKnown();
  }
  return true;
}

void DebuggedProcess::SuspendAndSendModulesIfKnown() {
  if (!module_list_.modules().empty()) {
    // This process' modules can be known. Send them.
    //
    // Suspend all threads while the module list is being sent. The client will resume the threads
    // once it's loaded symbols and processed breakpoints (this may take a while and we'd like to
    // get any breakpoints as early as possible).
    ClientSuspendAllThreads();
    SendModuleNotification();
  }
}

void DebuggedProcess::SendModuleNotification() {
  // Notify the client of any libraries.
  debug_ipc::NotifyModules notify;
  notify.process_koid = koid();
  notify.modules = module_list_.modules();
  notify.timestamp = GetNowTimestamp();

  // All threads are assumed to be stopped.
  for (auto& [thread_koid, thread_ptr] : threads_)
    notify.stopped_threads.push_back({.process = koid(), .thread = thread_koid});

  DEBUG_LOG(Process) << LogPreamble(this) << "Sending modules.";

  debug_agent_->stream()->Write(debug_ipc::SerializeNotifyModules(notify));
}

SoftwareBreakpoint* DebuggedProcess::FindSoftwareBreakpoint(uint64_t address) const {
  auto it = software_breakpoints_.find(address);
  if (it == software_breakpoints_.end())
    return nullptr;
  return it->second.get();
}

HardwareBreakpoint* DebuggedProcess::FindHardwareBreakpoint(uint64_t address) const {
  auto it = hardware_breakpoints_.find(address);
  if (it == hardware_breakpoints_.end())
    return nullptr;
  return it->second.get();
}

Watchpoint* DebuggedProcess::FindWatchpoint(const debug::AddressRange& range) const {
  auto it = watchpoints_.lower_bound(range);
  if (it == watchpoints_.end())
    return nullptr;

  for (; it != watchpoints_.end(); it++) {
    if (it->first.Contains(range))
      return it->second.get();
  }

  return nullptr;
}

debug::Status DebuggedProcess::RegisterBreakpoint(Breakpoint* bp, uint64_t address) {
  LogRegisterBreakpoint(FROM_HERE, this, bp, address);

  switch (bp->settings().type) {
    case debug_ipc::BreakpointType::kSoftware:
      return RegisterSoftwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kHardware:
      return RegisterHardwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kReadWrite:
    case debug_ipc::BreakpointType::kWrite:
      FX_NOTREACHED() << "Watchpoints are registered through RegisterWatchpoint.";
      // TODO(donosoc): Reactivate once the transition is complete.
      return debug::Status("Watchpoints are registered through RegisterWatchpoint.");
    case debug_ipc::BreakpointType::kLast:
      FX_NOTREACHED();
      return debug::Status("Invalid breakpoint type.");
  }

  FX_NOTREACHED();
}

void DebuggedProcess::UnregisterBreakpoint(Breakpoint* bp, uint64_t address) {
  DEBUG_LOG(Process) << LogPreamble(this) << "Unregistering breakpoint " << bp->settings().id
                     << " (" << bp->settings().name << ").";

  switch (bp->settings().type) {
    case debug_ipc::BreakpointType::kSoftware:
      return UnregisterSoftwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kHardware:
      return UnregisterHardwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kReadWrite:
    case debug_ipc::BreakpointType::kWrite:
      FX_NOTREACHED() << "Watchpoints are unregistered through UnregisterWatchpoint.";
      return;
    case debug_ipc::BreakpointType::kLast:
      FX_NOTREACHED();
      return;
  }

  FX_NOTREACHED();
}

debug::Status DebuggedProcess::RegisterWatchpoint(Breakpoint* bp,
                                                  const debug::AddressRange& range) {
  FX_DCHECK(debug_ipc::IsWatchpointType(bp->settings().type))
      << "Breakpoint type must be kWatchpoint, got: "
      << debug_ipc::BreakpointTypeToString(bp->settings().type);

  // NOTE: Even though the watchpoint system can handle un-aligned ranges, there is no way for
  //       an exception to determine which byte access actually triggered the exception. This means
  //       that watchpoint installed and nominal ranges should be the same.
  //
  //       We make that check here and fail early if the range is not correctly aligned.
  auto aligned_range = AlignRange(range);
  if (!aligned_range.has_value() || aligned_range.value() != range)
    return debug::Status("Watchpoint range must be aligned.");

  auto it = watchpoints_.find(range);
  if (it == watchpoints_.end()) {
    auto watchpoint = std::make_unique<Watchpoint>(bp->settings().type, bp, this, range);
    if (auto status = watchpoint->Init(); status.has_error())
      return status;

    watchpoints_[range] = std::move(watchpoint);
    return debug::Status();
  } else {
    return it->second->RegisterBreakpoint(bp);
  }
}

void DebuggedProcess::UnregisterWatchpoint(Breakpoint* bp, const debug::AddressRange& range) {
  FX_DCHECK(debug_ipc::IsWatchpointType(bp->settings().type))
      << "Breakpoint type must be kWatchpoint, got: "
      << debug_ipc::BreakpointTypeToString(bp->settings().type);

  auto it = watchpoints_.find(range);
  if (it == watchpoints_.end())
    return;

  Watchpoint* watchpoint = it->second.get();
  bool still_used = watchpoint->UnregisterBreakpoint(bp);
  if (!still_used) {
    for (auto& [thread_koid, thread] : threads_) {
      thread->WillDeleteProcessBreakpoint(watchpoint);
    }
  }

  watchpoints_.erase(it);
}

void DebuggedProcess::EnqueueStepOver(ProcessBreakpoint* process_breakpoint,
                                      DebuggedThread* thread) {
  // Passing the thread will delete any previous queuing of the same thread. Otherwise the thread
  // will be recusrsively waiting for itself and can never make progress.
  PruneStepOverQueue(thread);

  StepOverTicket ticket = {};
  ticket.process_breakpoint = process_breakpoint->GetWeakPtr();
  ticket.thread = thread->GetWeakPtr();
  step_over_queue_.push_back(std::move(ticket));

  DEBUG_LOG(Process) << LogPreamble(this) << "[PB: 0x" << std::hex << process_breakpoint->address()
                     << "] Enqueing thread " << std::dec << thread->koid()
                     << " for step over. Queue size: " << step_over_queue_.size();

  // If the queue already had an element, we wait until that element is done.
  if (step_over_queue_.size() > 1u)
    return;

  // This is the first ticket in the queue. We start executing it immediatelly.
  process_breakpoint->ExecuteStepOver(thread);
}

void DebuggedProcess::OnBreakpointFinishedSteppingOver() {
  {
    // We always tell the current breakpoint to finish the stepping over after starting the new one.
    // This will free the other suspended threads, letting the new stepping over thread continue.
    //
    // We need to do this *after* starting the following breakpoint, because otherwise we introduce
    // a window where threads are unsuspeded between breakpoints.
    auto prev_ticket = step_over_queue_.front();
    auto post_execute_breakpoint = fit::defer([prev_ticket = std::move(prev_ticket)]() {
      if (!prev_ticket.is_valid())
        return;

      prev_ticket.process_breakpoint->StepOverCleanup(prev_ticket.thread.get());
    });

    // Pop the previous ticket (the post-complete action is already set with the deferred action).
    step_over_queue_.pop_front();

    // If there are still elements in the queue, we execute the next one (the queue is pruned so we
    // know the next one is valid).
    PruneStepOverQueue(nullptr);
    if (!step_over_queue_.empty()) {
      auto& ticket = step_over_queue_.front();
      ticket.process_breakpoint->ExecuteStepOver(ticket.thread.get());
      return;
    }
  }
}

void DebuggedProcess::OnProcessTerminated() {
  DEBUG_LOG(Process) << LogPreamble(this) << "Terminating.";
  debug_ipc::NotifyProcessExiting notify;
  notify.process_koid = koid();
  notify.return_code = process_handle_->GetReturnCode();
  notify.timestamp = GetNowTimestamp();

  debug_agent_->stream()->Write(debug_ipc::SerializeNotifyProcessExiting(notify));

  debug_agent_->RemoveDebuggedProcess(koid());
  // "THIS" IS NOW DELETED.
}

void DebuggedProcess::OnThreadStarting(std::unique_ptr<ExceptionHandle> exception) {
  auto thread_handle = exception->GetThreadHandle();
  zx_koid_t thread_id = thread_handle->GetKoid();
  DEBUG_LOG(Process) << LogPreamble(this) << " Thread starting with koid " << thread_id;

  if (threads_.find(thread_id) != threads_.end()) {
    // This is possible when `DebugAgent::AttachToExistingProcess` in the following order:
    //   - We create an exception channel.
    //   - A thread starts and the notification is delivered to the exception channel,
    //     but we haven't got a chance to look into it.
    //   - We PopulateCurrentThreads().
    //   - We process the pending notifications and OnThreadStarting() the same thread
    //     populated just now.
    return;
  }

  auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, this, std::move(thread_handle));
  auto added = threads_.emplace(thread_id, std::move(new_thread));

  // Notify the client.
  added.first->second->SendThreadNotification();
}

void DebuggedProcess::OnThreadExiting(std::unique_ptr<ExceptionHandle> exception) {
  auto excepting_thread_handle = exception->GetThreadHandle();
  zx_koid_t thread_id = excepting_thread_handle->GetKoid();
  DEBUG_LOG(Process) << LogPreamble(this) << " Thread exiting with koid " << thread_id;

  // Clean up our DebuggedThread object.
  auto found_thread = threads_.find(thread_id);
  if (found_thread == threads_.end()) {
    FX_NOTREACHED();
    return;
  }

  // The thread will currently be in a "Dying" state. For it to complete its
  // lifecycle it must be resumed.
  exception.reset();

  threads_.erase(thread_id);

  // Notify the client. Can't call GetThreadRecord since the thread doesn't exist any more.
  debug_ipc::NotifyThread notify;
  notify.record.id = {.process = koid(), .thread = thread_id};
  notify.record.state = debug_ipc::ThreadRecord::State::kDead;
  notify.timestamp = GetNowTimestamp();

  debug_agent_->stream()->Write(debug_ipc::SerializeNotifyThreadExiting(notify));
}

void DebuggedProcess::OnException(std::unique_ptr<ExceptionHandle> exception) {
  auto excepting_thread_handle = exception->GetThreadHandle();
  zx_koid_t thread_id = excepting_thread_handle->GetKoid();

  DebuggedThread* thread = GetThread(thread_id);
  if (!thread) {
    LOGS(Error) << "Exception on thread " << thread_id << " which we don't know about.";
    return;
  }

  thread->OnException(std::move(exception));
}

void DebuggedProcess::OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                                     debug_ipc::AddressSpaceReply* reply) {
  reply->map = process_handle_->GetAddressSpace(request.address);
}

void DebuggedProcess::OnModules(debug_ipc::ModulesReply* reply) {
  // Since the client requested the modules explicitly, force update our cache in case something
  // changed unexpectedly.
  module_list_.Update(process_handle());
  reply->modules = module_list_.modules();
}

void DebuggedProcess::OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                                    debug_ipc::WriteMemoryReply* reply) {
  size_t actual = 0;

  // TODO(brettw) replace reply with a serialized Status.
  if (debug::Status status = process_handle_->WriteMemory(request.address, request.data.data(),
                                                          request.data.size(), &actual);
      status.has_error()) {
    reply->status = status;
  } else if (actual != request.data.size()) {
    // Convert partial writes to errors.
    reply->status = debug::Status("Partial write of " + std::to_string(actual) +
                                  " bytes instead of " + std::to_string(request.data.size()));
  } else {
    reply->status = debug::Status();
  }
}

void DebuggedProcess::OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                                            debug_ipc::LoadInfoHandleTableReply* reply) {
  auto result = process_handle_->GetHandles();
  if (result.is_error()) {
    // TODO(brettw) replace reply with a serialized status.
    reply->status = result.error_value();
  } else {
    reply->status = debug::Status();
    reply->handles = std::move(std::move(result).value());
  }
}

void DebuggedProcess::InjectThreadForTest(std::unique_ptr<DebuggedThread> thread) {
  zx_koid_t koid = thread->koid();
  threads_[koid] = std::move(thread);
}

std::vector<debug_ipc::ProcessThreadId> DebuggedProcess::ClientSuspendAllThreads(
    zx_koid_t except_thread) {
  std::vector<debug_ipc::ProcessThreadId> suspended_thread_ids;

  // Issue the suspension order for all the threads.
  for (auto& [thread_koid, thread] : threads_) {
    // Do an asynchronous suspend. We'll wait for the suspension at the bottom. If there is more
    // than one thread this allows waiting for each to complete in parallel instead of series.
    //
    // Here we explitly check for something already suspended, even if re-suspending it is a no-op,
    // because we don't want to report its state as changed.
    if (thread_koid != except_thread && !thread->is_client_suspended()) {
      suspended_thread_ids.push_back({.process = koid(), .thread = thread_koid});
      thread->ClientSuspend(false);
    }
  }

  // Wait on the notification for each thread.
  auto deadline = DebuggedThread::DefaultSuspendDeadline();
  for (const debug_ipc::ProcessThreadId& id : suspended_thread_ids) {
    if (DebuggedThread* thread = GetThread(id.thread))
      thread->thread_handle().WaitForSuspension(deadline);
  }

  return suspended_thread_ids;
}

void DebuggedProcess::SetStdout(OwnedStdioHandle handle) {
  stdout_ = std::make_unique<BufferedStdioHandle>(std::move(handle));
  // We bind |this| into the callbacks. This is OK because the DebuggedProcess
  // owns both sockets, meaning that it's assured to outlive the sockets.
  stdout_->set_data_available_callback([this]() { OnStdout(false); });
  stdout_->set_error_callback([this]() { OnStdout(true); });
  if (!stdout_->Start()) {
    LOGS(Warn) << "Could not listen on stdout for process " << process_handle_->GetName();
    stdout_.reset();
  }
}

void DebuggedProcess::SetStderr(OwnedStdioHandle handle) {
  stderr_ = std::make_unique<BufferedStdioHandle>(std::move(handle));
  stderr_->set_data_available_callback([this]() { OnStderr(false); });
  stderr_->set_error_callback([this]() { OnStderr(true); });
  if (!stderr_->Start()) {
    LOGS(Warn) << "Could not listen on stderr for process " << process_handle_->GetName();
    stderr_.reset();
  }
}

void DebuggedProcess::OnStdout(bool close) {
  FX_DCHECK(stdout_ && stdout_->IsValid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stdout closed.";
    stdout_.reset();
    return;
  }

  auto data = ReadSocketInput(stdout_.get());
  FX_DCHECK(!data.empty());
  DEBUG_LOG(Process) << LogPreamble(this)
                     << "Got stdout: " << std::string(data.data(), data.size());
  SendIO(debug_ipc::NotifyIO::Type::kStdout, std::move(data));
}

void DebuggedProcess::OnStderr(bool close) {
  FX_DCHECK(stderr_ && stderr_->IsValid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stderr closed.";
    stderr_.reset();
    return;
  }

  auto data = ReadSocketInput(stderr_.get());
  FX_DCHECK(!data.empty());
  DEBUG_LOG(Process) << LogPreamble(this)
                     << "Got stderr: " << std::string(data.data(), data.size());
  SendIO(debug_ipc::NotifyIO::Type::kStderr, std::move(data));
}

void DebuggedProcess::SendIO(debug_ipc::NotifyIO::Type type, const std::vector<char>& data) {
  // We send the IO message in chunks.
  auto it = data.begin();
  size_t size = data.size();
  while (size > 0) {
    size_t chunk_size = size;
    if (chunk_size >= debug_ipc::NotifyIO::kMaxDataSize)
      chunk_size = debug_ipc::NotifyIO::kMaxDataSize;

    auto end = it + chunk_size;
    std::string msg(it, end);

    it = end;
    size -= chunk_size;

    debug_ipc::NotifyIO notify;
    notify.process_koid = koid();
    notify.type = type;
    // We tell whether this is a piece of a bigger message.
    notify.more_data_available = size > 0;
    notify.data = std::move(msg);
    notify.timestamp = GetNowTimestamp();

    debug_agent_->stream()->Write(debug_ipc::SerializeNotifyIO(notify));
  }
}

void DebuggedProcess::PruneStepOverQueue(DebuggedThread* optional_thread) {
  std::deque<StepOverTicket> good_tickets;
  for (auto& ticket : step_over_queue_) {
    if (!ticket.is_valid())
      continue;
    if (optional_thread && ticket.thread && ticket.thread.get() == optional_thread)
      continue;  // Delete everything from this thread.
    good_tickets.push_back(std::move(ticket));
  }

  step_over_queue_ = std::move(good_tickets);
}

debug::Status DebuggedProcess::RegisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = software_breakpoints_.find(address);
  if (found == software_breakpoints_.end()) {
    auto breakpoint = std::make_unique<SoftwareBreakpoint>(bp, this, address);
    if (auto status = breakpoint->Init(); status.has_error())
      return status;

    software_breakpoints_[address] = std::move(breakpoint);
    return debug::Status();
  } else {
    return found->second->RegisterBreakpoint(bp);
  }
}

void DebuggedProcess::UnregisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = software_breakpoints_.find(address);
  if (found == software_breakpoints_.end()) {
    return;
  }

  bool still_used = found->second->UnregisterBreakpoint(bp);
  if (!still_used) {
    for (auto& pair : threads_)
      pair.second->WillDeleteProcessBreakpoint(found->second.get());
    software_breakpoints_.erase(found);
  }
}

debug::Status DebuggedProcess::RegisterHardwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = hardware_breakpoints_.find(address);
  if (found == hardware_breakpoints_.end()) {
    auto breakpoint = std::make_unique<HardwareBreakpoint>(bp, this, address);
    if (auto status = breakpoint->Init(); status.has_error())
      return status;

    hardware_breakpoints_[address] = std::move(breakpoint);
    return debug::Status();
  } else {
    return found->second->RegisterBreakpoint(bp);
  }
}

void DebuggedProcess::UnregisterHardwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = hardware_breakpoints_.find(address);
  if (found == hardware_breakpoints_.end()) {
    return;
  }

  bool still_used = found->second->UnregisterBreakpoint(bp);
  if (!still_used) {
    for (auto& pair : threads_)
      pair.second->WillDeleteProcessBreakpoint(found->second.get());
    hardware_breakpoints_.erase(found);
  }
}

}  // namespace debug_agent
