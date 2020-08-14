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
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

std::vector<char> ReadSocketInput(debug_ipc::BufferedZxSocket* socket) {
  FX_DCHECK(socket->valid());

  constexpr size_t kReadSize = 1024;  // Read in 1K chunks.

  std::vector<char> data;
  auto& stream = socket->stream();
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

void LogRegisterBreakpoint(debug_ipc::FileLineFunction location, DebuggedProcess* process,
                           Breakpoint* bp, uint64_t address) {
  if (!debug_ipc::IsDebugModeActive())
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

DebuggedProcess::DebuggedProcess(DebugAgent* debug_agent, DebuggedProcessCreateInfo&& create_info)
    : debug_agent_(debug_agent),
      process_handle_(std::move(create_info.handle)),
      from_limbo_(create_info.from_limbo) {
  // If create_info out or err are not valid, calling Init on the
  // BufferedZxSocket will fail and leave it in an invalid state. This is
  // expected if the io sockets could be obtained from the inferior.
  stdout_.Init(std::move(create_info.out));
  stderr_.Init(std::move(create_info.err));
}

DebuggedProcess::~DebuggedProcess() { DetachFromProcess(); }

void DebuggedProcess::DetachFromProcess() {
  // 1. Remove installed software breakpoints.
  //    We need to tell each thread that this will happen.
  for (auto& [address, breakpoint] : software_breakpoints_) {
    for (auto& [thread_koid, thread] : threads_) {
      thread->WillDeleteProcessBreakpoint(breakpoint.get());
    }
  }

  // Clear the resources.
  software_breakpoints_.clear();
  hardware_breakpoints_.clear();
  watchpoints_.clear();

  // 2. Resume threads.
  // Technically a 0'ed request would work, but being explicit is future-proof.
  debug_ipc::ResumeRequest resume_request = {};
  resume_request.how = debug_ipc::ResumeRequest::How::kResolveAndContinue;
  resume_request.process_koid = koid();
  OnResume(resume_request);

  // 3. Unbind from notifications (this will detach from the process).
  process_handle_->Detach();
}

zx_status_t DebuggedProcess::Init() {
  RegisterDebugState();

  // Watch for process events.
  if (zx_status_t status = process_handle_->Attach(this); status != ZX_OK)
    return status;

  // Binding stdout/stderr.
  // We bind |this| into the callbacks. This is OK because the DebuggedProcess
  // owns both sockets, meaning that it's assured to outlive the sockets.

  if (stdout_.valid()) {
    stdout_.set_data_available_callback([this]() { OnStdout(false); });
    stdout_.set_error_callback([this]() { OnStdout(true); });
    if (zx_status_t status = stdout_.Start(); status != ZX_OK) {
      FX_LOGS(WARNING) << "Could not listen on stdout for process " << process_handle_->GetName()
                       << ": " << debug_ipc::ZxStatusToString(status);
      stdout_.Reset();
    }
  }

  if (stderr_.valid()) {
    stderr_.set_data_available_callback([this]() { OnStderr(false); });
    stderr_.set_error_callback([this]() { OnStderr(true); });
    if (zx_status_t status = stderr_.Start(); status != ZX_OK) {
      FX_LOGS(WARNING) << "Could not listen on stderr for process " << process_handle_->GetName()
                       << ": " << debug_ipc::ZxStatusToString(status);
      stderr_.Reset();
    }
  }

  return ZX_OK;
}

void DebuggedProcess::OnPause(const debug_ipc::PauseRequest& request,
                              debug_ipc::PauseReply* reply) {
  // This function should do a best effort to ensure the thread(s) are actually stopped before the
  // reply is sent. This will ensure that other commands the client may have enqueued operate on a
  // suspended thread as they may be expecting.
  if (request.thread_koid) {
    DebuggedThread* thread = GetThread(request.thread_koid);
    if (thread) {
      thread->ClientSuspend(true);

      // The Suspend call could have failed though most failures should be rare (perhaps we raced
      // with the thread being destroyed). Either way, send our current knowledge of the thread's
      // state.
      reply->threads.push_back(
          thread->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
    }
    // Could be not found if there is a race between the thread exiting and
    // the client sending the request.
  } else {
    // 0 thread ID means pause all threads.
    ClientSuspendAllThreads();
    reply->threads = GetThreadRecords();
  }
}

void DebuggedProcess::OnResume(const debug_ipc::ResumeRequest& request) {
  if (request.thread_koids.empty()) {
    // Empty thread ID list means resume all threads.
    for (auto& [thread_koid, thread] : threads_)
      thread->ClientResume(request);
  } else {
    for (uint64_t thread_koid : request.thread_koids) {
      if (DebuggedThread* thread = GetThread(thread_koid))
        thread->ClientResume(request);
      // Might be not found if there is a race between the thread exiting and the client sending the
      // request.
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

    auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, this, std::move(thread),
                                                       ThreadCreationOption::kRunningKeepRunning);
    threads_.emplace(thread_koid, std::move(new_thread));
  }
}

std::vector<debug_ipc::ThreadRecord> DebuggedProcess::GetThreadRecords() const {
  std::vector<debug_ipc::ThreadRecord> result;
  for (const auto& pair : threads_)
    result.push_back(pair.second->GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal));
  return result;
}

bool DebuggedProcess::RegisterDebugState() {
  // HOW REGISTRATION WITH THE LOADER WORKS.
  //
  // Upon process initialization and before executing the normal program code, ld.so sets the
  // ZX_PROP_PROCESS_DEBUG_ADDR property on its own process to the address of a known struct defined
  // in <link.h> containing the state of the loader. Debuggers can come along later, get the address
  // from this property, and inspect the state of the dynamic loader for this process (get the
  // loaded libraries, set breakpoints for loads, etc.).
  //
  // When launching a process in a debugger, the debugger needs to know when this property has been
  // set or there will be a race to know when it's valid. To resolve this, the debuggers sets a
  // known magic value to the property before startup. The loader checks for this value when setting
  // the property, and if it had the magic value, issues a hardcoded software breakpoint. The
  // debugger catches this breakpoint exception, reads the now-valid address from the property, and
  // continues initialization.
  //
  // It's also possible that the property has been properly set up prior to starting the process.
  // In Posix this can happen with a fork() where the entire process is duplicated, including the
  // loader state and all dynamically loaded libraries. In Zircon this can happen if the creator
  // of the process maps a valid loader state when it creates the process (possibly it's trying
  // to emulate fork, or it could be injecting libraries itself for some reason). So we also need
  // to handle the rare case that the propery is set before startup.
  if (dl_debug_addr_)
    return true;  // Previously set.

  uintptr_t debug_addr = 0;
  if (handle().get_property(ZX_PROP_PROCESS_DEBUG_ADDR, &debug_addr, sizeof(debug_addr)) != ZX_OK ||
      debug_addr == 0) {
    // Register for sets on the debug addr by setting the magic value.
    const intptr_t kMagicValue = ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET;
    handle().set_property(ZX_PROP_PROCESS_DEBUG_ADDR, &kMagicValue, sizeof(kMagicValue));
    return false;
  }
  if (debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET)
    return false;  // Still not set.

  dl_debug_addr_ = debug_addr;

  // Register a breakpoint for dynamic loads.
  if (auto load_addr = GetLoaderBreakpointAddress(process_handle(), dl_debug_addr_)) {
    loader_breakpoint_ = std::make_unique<Breakpoint>(debug_agent_, true);
    if (loader_breakpoint_->SetSettings("Internal shared library load breakpoint", koid(),
                                        load_addr) != ZX_OK) {
      DEBUG_LOG(Process) << LogPreamble(this) << "Could not set shared library load breakpoint at "
                         << std::hex << load_addr;
      // Continue even in the error case: we can continue with most things working even if the
      // loader breakpoint fails for some reason.
    }
  }

  return true;
}

DebuggedProcess::SpecialBreakpointResult DebuggedProcess::HandleSpecialBreakpoint(
    ProcessBreakpoint* optional_bp) {
  // The special Fuchsia loader breakpoint will be a hardcodeed breakpoint (so no input
  // ProcessBreakpoint object) before we've seen the dl_debug_addr_.
  if (!dl_debug_addr_ && !optional_bp) {
    if (RegisterDebugState()) {
      // Register our state and continue transparently.
      if (module_list_.Update(process_handle(), dl_debug_addr_)) {
        // The initial loader breakpoint will happen very early in the process startup so it
        // will be single threaded. Since the one thread is already stopped, we can skip suspending
        // the threads and just notify the client, keeping the calling one suspended.
        SendModuleNotification();
        return SpecialBreakpointResult::kKeepSuspended;
      }

      // Modules haven't changed, resume.
      return SpecialBreakpointResult::kContinue;
    }
  }

  // Our special loader breakpoint is a breakpoint we've inserted for every shared library load.
  if (optional_bp) {
    const auto& breakpoints = optional_bp->breakpoints();
    if (std::find(breakpoints.begin(), breakpoints.end(), loader_breakpoint_.get()) !=
        breakpoints.end()) {
      if (module_list_.Update(process_handle(), dl_debug_addr_)) {
        // The debugged process could be multithreaded and have just dynamically loaded a new
        // module. Suspend all threads so the client can resolve breakpoint addresses before
        // continuing.
        SuspendAndSendModulesIfKnown();
        return SpecialBreakpointResult::kKeepSuspended;
      }

      // Modules haven't changed, resume.
      return SpecialBreakpointResult::kContinue;
    }
  }

  // Not one of our special breakpoints.
  return SpecialBreakpointResult::kNotSpecial;
}

void DebuggedProcess::SuspendAndSendModulesIfKnown() {
  if (dl_debug_addr_) {
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

  // All threads are assumed to be stopped.
  for (auto& [thread_koid, thread_ptr] : threads_)
    notify.stopped_thread_koids.push_back(thread_koid);

  DEBUG_LOG(Process) << LogPreamble(this) << "Sending modules.";

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyModules(notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
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

Watchpoint* DebuggedProcess::FindWatchpoint(const debug_ipc::AddressRange& range) const {
  auto it = watchpoints_.lower_bound(range);
  if (it == watchpoints_.end())
    return nullptr;

  for (; it != watchpoints_.end(); it++) {
    if (it->first.Contains(range))
      return it->second.get();
  }

  return nullptr;
}

zx_status_t DebuggedProcess::RegisterBreakpoint(Breakpoint* bp, uint64_t address) {
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
      return ZX_ERR_INVALID_ARGS;
    case debug_ipc::BreakpointType::kLast:
      FX_NOTREACHED();
      return ZX_ERR_INVALID_ARGS;
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

zx_status_t DebuggedProcess::RegisterWatchpoint(Breakpoint* bp,
                                                const debug_ipc::AddressRange& range) {
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
    return ZX_ERR_INVALID_ARGS;

  auto it = watchpoints_.find(range);
  if (it == watchpoints_.end()) {
    auto watchpoint = std::make_unique<Watchpoint>(bp->settings().type, bp, this, range);
    if (zx_status_t status = watchpoint->Init(); status != ZX_OK)
      return status;

    watchpoints_[range] = std::move(watchpoint);
    return ZX_OK;
  } else {
    return it->second->RegisterBreakpoint(bp);
  }
}

void DebuggedProcess::UnregisterWatchpoint(Breakpoint* bp, const debug_ipc::AddressRange& range) {
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
  PruneStepOverQueue();

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

    // If there are still elements in the queue, we execute the next one.
    // Since the queue is pruned,
    PruneStepOverQueue();
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

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessExiting(notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());

  debug_agent_->RemoveDebuggedProcess(koid());
  // "THIS" IS NOW DELETED.
}

void DebuggedProcess::OnThreadStarting(std::unique_ptr<ExceptionHandle> exception) {
  auto thread_handle = exception->GetThreadHandle();
  zx_koid_t thread_id = thread_handle->GetKoid();
  DEBUG_LOG(Process) << LogPreamble(this) << " Thread starting with koid " << thread_id;

  // Shouldn't have this thread yet.
  FX_DCHECK(threads_.find(thread_id) == threads_.end());

  auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, this, std::move(thread_handle),
                                                     ThreadCreationOption::kSuspendedKeepSuspended,
                                                     std::move(exception));
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
  notify.record.process_koid = koid();
  notify.record.thread_koid = thread_id;
  notify.record.state = debug_ipc::ThreadRecord::State::kDead;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadExiting, notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedProcess::OnException(std::unique_ptr<ExceptionHandle> exception) {
  auto excepting_thread_handle = exception->GetThreadHandle();
  zx_koid_t thread_id = excepting_thread_handle->GetKoid();

  DebuggedThread* thread = GetThread(thread_id);
  if (!thread) {
    FX_LOGS(ERROR) << "Exception on thread " << thread_id << " which we don't know about.";
    return;
  }

  thread->OnException(std::move(exception));
}

void DebuggedProcess::OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                                     debug_ipc::AddressSpaceReply* reply) {
  reply->map = process_handle_->GetAddressSpace(request.address);
}

void DebuggedProcess::OnModules(debug_ipc::ModulesReply* reply) {
  // Modules can only be read after the debug state is set.
  if (dl_debug_addr_)
    reply->modules = process_handle_->GetModules(dl_debug_addr_);
}

void DebuggedProcess::OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                                    debug_ipc::WriteMemoryReply* reply) {
  size_t actual = 0;
  reply->status =
      process_handle_->WriteMemory(request.address, &request.data[0], request.data.size(), &actual);
  if (reply->status == ZX_OK && actual != request.data.size())
    reply->status = ZX_ERR_IO;  // Convert partial writes to errors.
}

void DebuggedProcess::OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                                            debug_ipc::LoadInfoHandleTableReply* reply) {
  auto result = process_handle_->GetHandles();
  if (result.is_error()) {
    reply->status = result.error_value();
  } else {
    reply->status = ZX_OK;
    reply->handles = std::move(std::move(result).value());
  }
}

void DebuggedProcess::InjectThreadForTest(std::unique_ptr<DebuggedThread> thread) {
  zx_koid_t koid = thread->koid();
  threads_[koid] = std::move(thread);
}

void DebuggedProcess::ClientSuspendAllThreads() {
  // Issue the suspension order for all the threads.
  for (auto& [thread_koid, thread] : threads_) {
    // Do an asynchronous suspend. We'll wait for the suspension at the bottom. If there is more
    // than one thread this allows waiting for each to complete in parallel instead of series.
    thread->ClientSuspend(false);
  }

  // Wait on the notification for each thread.
  zx::time deadline = DebuggedThread::DefaultSuspendDeadline();
  for (auto& [thread_koid, thread] : threads_)
    thread->thread_handle().WaitForSuspension(deadline);
}

void DebuggedProcess::OnStdout(bool close) {
  FX_DCHECK(stdout_.valid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stdout closed.";
    stdout_.Reset();
    return;
  }

  auto data = ReadSocketInput(&stdout_);
  FX_DCHECK(!data.empty());
  DEBUG_LOG(Process) << LogPreamble(this)
                     << "Got stdout: " << std::string(data.data(), data.size());
  SendIO(debug_ipc::NotifyIO::Type::kStdout, std::move(data));
}

void DebuggedProcess::OnStderr(bool close) {
  FX_DCHECK(stderr_.valid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stderr closed.";
    stderr_.Reset();
    return;
  }

  auto data = ReadSocketInput(&stderr_);
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

    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyIO(notify, &writer);
    debug_agent_->stream()->Write(writer.MessageComplete());
  }
}

void DebuggedProcess::PruneStepOverQueue() {
  std::deque<StepOverTicket> pruned_tickets;
  for (auto& ticket : step_over_queue_) {
    if (!ticket.is_valid())
      continue;
    pruned_tickets.push_back(std::move(ticket));
  }

  step_over_queue_ = std::move(pruned_tickets);
}

zx_status_t DebuggedProcess::RegisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = software_breakpoints_.find(address);
  if (found == software_breakpoints_.end()) {
    auto breakpoint = std::make_unique<SoftwareBreakpoint>(bp, this, address);
    if (zx_status_t status = breakpoint->Init(); status != ZX_OK)
      return status;

    software_breakpoints_[address] = std::move(breakpoint);
    return ZX_OK;
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

zx_status_t DebuggedProcess::RegisterHardwareBreakpoint(Breakpoint* bp, uint64_t address) {
  auto found = hardware_breakpoints_.find(address);
  if (found == hardware_breakpoints_.end()) {
    auto breakpoint = std::make_unique<HardwareBreakpoint>(bp, this, address);
    if (zx_status_t status = breakpoint->Init(); status != ZX_OK)
      return status;

    hardware_breakpoints_[address] = std::move(breakpoint);
    return ZX_OK;
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
