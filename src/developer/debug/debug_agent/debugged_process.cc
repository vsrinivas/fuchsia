// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_process.h"

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <zircon/syscalls/exception.h>

#include <utility>

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

std::vector<char> ReadSocketInput(debug_ipc::BufferedZxSocket* socket) {
  FXL_DCHECK(socket->valid());

  constexpr size_t kReadSize = 1024;  // Read in 1K chunks.

  std::vector<char> data;
  auto& stream = socket->stream();
  while (true) {
    char buf[kReadSize];

    // Add a zero at the end just in case.
    size_t read_amount = stream.Read(buf, kReadSize);
    data.insert(data.end(), buf, buf + read_amount);

    if (read_amount < kReadSize)
      break;
  }

  return data;
}

// Meant to be used in debug logging.
std::string LogPreamble(const DebuggedProcess* process) {
  return fxl::StringPrintf("[P: %lu (%s)] ", process->koid(), process->name().c_str());
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

DebuggedProcessCreateInfo::DebuggedProcessCreateInfo() = default;
DebuggedProcessCreateInfo::DebuggedProcessCreateInfo(zx_koid_t process_koid,
                                                     std::string process_name, zx::process handle)
    : koid(process_koid), handle(std::move(handle)), name(std::move(process_name)) {}

DebuggedProcessCreateInfo::DebuggedProcessCreateInfo(
    zx_koid_t process_koid, std::string process_name, zx::process handle,
    std::shared_ptr<arch::ArchProvider> arch_provider,
    std::shared_ptr<ObjectProvider> object_provider)
    : koid(process_koid),
      handle(std::move(handle)),
      arch_provider(std::move(arch_provider)),
      object_provider(std::move(object_provider)),
      name(std::move(process_name)) {}

// DebuggedProcessMemoryAccessor -----------------------------------------------------------------

// MemoryAccessor geared towards a particular process. Created by the process if no override is
// provided at construction time. See DebuggedProcessCreateInfo above for more information.
class DebuggedProcessMemoryAccessor : public ProcessMemoryAccessor {
 public:
  explicit DebuggedProcessMemoryAccessor(DebuggedProcess* process) : process_(process) {}

  // ProcessMemoryAccessor implementation.
  zx_status_t ReadProcessMemory(uintptr_t address, void* buffer, size_t len,
                                size_t* actual) override {
    return process_->handle().read_memory(address, buffer, len, actual);
  }

  zx_status_t WriteProcessMemory(uintptr_t address, const void* buffer, size_t len,
                                 size_t* actual) override {
    return process_->handle().write_memory(address, buffer, len, actual);
  }

 private:
  DebuggedProcess* process_;  // Not owning. Must outline.
};

// DebuggedProcess ---------------------------------------------------------------------------------

DebuggedProcess::DebuggedProcess(DebugAgent* debug_agent, DebuggedProcessCreateInfo&& create_info)
    : arch_provider_(std::move(create_info.arch_provider)),
      object_provider_(std::move(create_info.object_provider)),
      memory_accessor_(std::move(create_info.memory_accessor)),
      debug_agent_(debug_agent),
      koid_(create_info.koid),
      handle_(std::move(create_info.handle)),
      name_(std::move(create_info.name)) {
  // If no overriden memory accessor was given, we create one that's geared towards this process.
  // See DebuggedProcessCreateInfo on the header for more information.
  if (!memory_accessor_)
    memory_accessor_ = std::make_unique<DebuggedProcessMemoryAccessor>(this);

  RegisterDebugState();

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
  software_breakpoints_.clear();

  // TODO(donosoc): Remove HW breakpoints.
  // TODO(donosoc): Remove Watchpoints.

  // 2. Resume threads.
  // Technically a 0'ed request would work, but being explicit is future-proof.
  debug_ipc::ResumeRequest resume_request = {};
  resume_request.how = debug_ipc::ResumeRequest::How::kContinue;
  resume_request.process_koid = koid_;
  OnResume(resume_request);

  // 3. Unbind from the exception port.
  process_watch_handle_.StopWatching();
}

zx_status_t DebuggedProcess::Init() {
  debug_ipc::MessageLoopTarget* loop = debug_ipc::MessageLoopTarget::Current();
  FXL_DCHECK(loop);  // Loop must be created on this thread first.

  // Register for debug exceptions.
  debug_ipc::MessageLoopTarget::WatchProcessConfig config;
  config.process_name = object_provider_->NameForObject(handle_);
  config.process_handle = handle_.get();
  config.process_koid = koid_;
  config.watcher = this;
  zx_status_t status = loop->WatchProcessExceptions(std::move(config), &process_watch_handle_);
  if (status != ZX_OK)
    return status;

  // Binding stdout/stderr.
  // We bind |this| into the callbacks. This is OK because the DebuggedProcess
  // owns both sockets, meaning that it's assured to outlive the sockets.

  if (stdout_.valid()) {
    stdout_.set_data_available_callback([this]() { OnStdout(false); });
    stdout_.set_error_callback([this]() { OnStdout(true); });
    status = stdout_.Start();
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Could not listen on stdout for process " << name_ << ": "
                       << debug_ipc::ZxStatusToString(status);
      stdout_.Reset();
    }
  }

  if (stderr_.valid()) {
    stderr_.set_data_available_callback([this]() { OnStderr(false); });
    stderr_.set_error_callback([this]() { OnStderr(true); });
    status = stderr_.Start();
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Could not listen on stderr for process " << name_ << ": "
                       << debug_ipc::ZxStatusToString(status);
      stderr_.Reset();
    }
  }

  return ZX_OK;
}

void DebuggedProcess::OnPause(const debug_ipc::PauseRequest& request,
                              debug_ipc::PauseReply* reply) {
  // This function should do a best effort to ensure the thread(s) are actually
  // stopped before the reply is sent.
  if (request.thread_koid) {
    DebuggedThread* thread = GetThread(request.thread_koid);
    if (thread) {
      thread->Suspend(true);
      thread->set_client_state(DebuggedThread::ClientState::kPaused);

      // The Suspend call could have failed though most failures should be
      // rare (perhaps we raced with the thread being destroyed). Either way,
      // send our current knowledge of the thread's state.
      debug_ipc::ThreadRecord record;
      thread->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, nullptr, &record);
      reply->threads.push_back(std::move(record));
    }
    // Could be not found if there is a race between the thread exiting and
    // the client sending the request.
  } else {
    // 0 thread ID means pause all threads.
    std::vector<zx_koid_t> suspended_koids;
    SuspendAll(true, &suspended_koids);

    // Change the state of those threads.
    for (zx_koid_t thread_koid : suspended_koids) {
      DebuggedThread* thread = GetThread(thread_koid);
      FXL_DCHECK(thread);
      thread->set_client_state(DebuggedThread::ClientState::kPaused);
    }

    FillThreadRecords(&reply->threads);
  }
}

void DebuggedProcess::OnResume(const debug_ipc::ResumeRequest& request) {
  if (request.thread_koids.empty()) {
    // Empty thread ID list means resume all threads.
    for (auto& [thread_koid, thread] : threads_) {
      thread->Resume(request);
      thread->set_client_state(DebuggedThread::ClientState::kRunning);
    }
  } else {
    for (uint64_t thread_koid : request.thread_koids) {
      DebuggedThread* thread = GetThread(thread_koid);
      if (thread) {
        thread->Resume(request);
        thread->set_client_state(DebuggedThread::ClientState::kRunning);
      }
      // Could be not found if there is a race between the thread exiting and
      // the client sending the request.
    }
  }
}

void DebuggedProcess::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                                   debug_ipc::ReadMemoryReply* reply) {
  ReadProcessMemoryBlocks(handle_, request.address, request.size, &reply->blocks);

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
  // Remove the watch handle before killing the process to avoid getting
  // exceptions after we stopped listening to them.
  process_watch_handle_ = {};

  // Since we're being killed, we treat this process as not having any more
  // threads. This makes cleanup code more straightforward, as there are no
  // threads to resume/handle.
  threads_.clear();
  reply->status = handle_.kill();
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
  for (zx_koid_t koid : object_provider_->GetChildKoids(handle_.get(), ZX_INFO_PROCESS_THREADS)) {
    // We should never populate the same thread twice.
    if (threads_.find(koid) != threads_.end())
      continue;

    zx_handle_t handle;
    if (object_provider_->GetChild(handle_.get(), koid, ZX_RIGHT_SAME_RIGHTS, &handle) == ZX_OK) {
      DebuggedThread::CreateInfo create_info = {};
      create_info.process = this;
      create_info.koid = koid;
      create_info.handle = zx::thread(handle);
      create_info.creation_option = ThreadCreationOption::kRunningKeepRunning;
      create_info.arch_provider = arch_provider_;
      create_info.object_provider = object_provider_;

      auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, std::move(create_info));
      threads_.emplace(koid, std::move(new_thread));
    }
  }
}

void DebuggedProcess::FillThreadRecords(std::vector<debug_ipc::ThreadRecord>* threads) {
  for (const auto& pair : threads_) {
    debug_ipc::ThreadRecord record;
    pair.second->FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, nullptr, &record);
    threads->push_back(std::move(record));
  }
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
  if (handle_.get_property(ZX_PROP_PROCESS_DEBUG_ADDR, &debug_addr, sizeof(debug_addr)) != ZX_OK ||
      debug_addr == 0) {
    // Register for sets on the debug addr by setting the magic value.
    const intptr_t kMagicValue = ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET;
    handle_.set_property(ZX_PROP_PROCESS_DEBUG_ADDR, &kMagicValue, sizeof(kMagicValue));
    return false;
  }
  if (debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET)
    return false;  // Still not set.

  dl_debug_addr_ = debug_addr;

  // TODO(brettw) register breakpoint for dynamic loads. This current code
  // only notifies for the initial set of binaries loaded by the process.
  return true;
}

void DebuggedProcess::SuspendAndSendModulesIfKnown() {
  if (dl_debug_addr_) {
    // This process' modules can be known. Send them.
    //
    // Suspend all threads while the module list is being sent. The client will resume the threads
    // once it's loaded symbols and processed breakpoints (this may take a while and we'd like to
    // get any breakpoints as early as possible).
    std::vector<uint64_t> paused_thread_koids;
    SuspendAll(false, &paused_thread_koids);
    SendModuleNotification(std::move(paused_thread_koids));
  }
}

void DebuggedProcess::SendModuleNotification(std::vector<uint64_t> paused_thread_koids) {
  // Notify the client of any libraries.
  debug_ipc::NotifyModules notify;
  notify.process_koid = koid_;
  GetModulesForProcess(handle_, dl_debug_addr_, &notify.modules);
  notify.stopped_thread_koids = std::move(paused_thread_koids);

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

  switch (bp->type()) {
    case debug_ipc::BreakpointType::kSoftware:
      return RegisterSoftwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kHardware:
      return RegisterHardwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kWatchpoint:
      FXL_NOTREACHED() << "Watchpoints are registered through RegisterWatchpoint.";
      // TODO(donosoc): Reactivate once the transition is complete.
      return ZX_ERR_INVALID_ARGS;
    case debug_ipc::BreakpointType::kLast:
      FXL_NOTREACHED();
      return ZX_ERR_INVALID_ARGS;
  }

  FXL_NOTREACHED();
}

void DebuggedProcess::UnregisterBreakpoint(Breakpoint* bp, uint64_t address) {
  DEBUG_LOG(Process) << LogPreamble(this) << "Unregistering breakpoint " << bp->settings().id
                     << " (" << bp->settings().name << ").";

  switch (bp->type()) {
    case debug_ipc::BreakpointType::kSoftware:
      return UnregisterSoftwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kHardware:
      return UnregisterHardwareBreakpoint(bp, address);
    case debug_ipc::BreakpointType::kWatchpoint:
      // TODO(donosoc): Reactivate once the transition is complete.
      return;
    case debug_ipc::BreakpointType::kLast:
      FXL_NOTREACHED();
      return;
  }

  FXL_NOTREACHED();
}

zx_status_t DebuggedProcess::RegisterWatchpoint(Breakpoint* bp,
                                                const debug_ipc::AddressRange& range) {
  FXL_DCHECK(bp->type() == debug_ipc::BreakpointType::kWatchpoint)
      << "Breakpoint type must be kWatchpoint, got: "
      << debug_ipc::BreakpointTypeToString(bp->type());

  auto it = watchpoints_.find(range);
  if (it == watchpoints_.end()) {
    auto watchpoint = std::make_unique<Watchpoint>(bp, this, arch_provider_, range);
    if (zx_status_t status = watchpoint->Init(); status != ZX_OK)
      return status;

    watchpoints_[range] = std::move(watchpoint);
    return ZX_OK;
  } else {
    return it->second->RegisterBreakpoint(bp);
  }
}

void DebuggedProcess::UnregisterWatchpoint(Breakpoint* bp, const debug_ipc::AddressRange& range) {
  FXL_DCHECK(bp->type() == debug_ipc::BreakpointType::kWatchpoint)
      << "Breakpoint type must be kWatchpoint, got: "
      << debug_ipc::BreakpointTypeToString(bp->type());

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

void DebuggedProcess::OnProcessTerminated(zx_koid_t process_koid) {
  DEBUG_LOG(Process) << LogPreamble(this) << "Terminating.";
  debug_ipc::NotifyProcessExiting notify;
  notify.process_koid = process_koid;

  zx_info_process info;
  GetProcessInfo(handle_.get(), &info);
  notify.return_code = info.return_code;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyProcessExiting(notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());

  debug_agent_->RemoveDebuggedProcess(process_koid);
  // "THIS" IS NOW DELETED.
}

void DebuggedProcess::OnThreadStarting(zx::exception exception,
                                       zx_exception_info_t exception_info) {
  FXL_DCHECK(exception_info.pid == koid());
  FXL_DCHECK(threads_.find(exception_info.tid) == threads_.end());

  zx::thread handle = object_provider_->GetThreadFromException(exception.get());

  DebuggedThread::CreateInfo create_info = {};
  create_info.process = this;
  create_info.koid = exception_info.tid;
  create_info.handle = std::move(handle);
  create_info.exception = std::move(exception);
  create_info.creation_option = ThreadCreationOption::kSuspendedKeepSuspended;
  create_info.arch_provider = arch_provider_;
  create_info.object_provider = object_provider_;

  auto new_thread = std::make_unique<DebuggedThread>(debug_agent_, std::move(create_info));
  auto added = threads_.emplace(exception_info.tid, std::move(new_thread));

  // Notify the client.
  added.first->second->SendThreadNotification();
}

void DebuggedProcess::OnThreadExiting(zx::exception exception, zx_exception_info_t exception_info) {
  FXL_DCHECK(exception_info.pid == koid());

  // Clean up our DebuggedThread object.
  auto found_thread = threads_.find(exception_info.tid);
  if (found_thread == threads_.end()) {
    FXL_NOTREACHED();
    return;
  }

  // The thread will currently be in a "Dying" state. For it to complete its
  // lifecycle it must be resumed.
  exception.reset();

  threads_.erase(exception_info.tid);

  // Notify the client. Can't call FillThreadRecord since the thread doesn't
  // exist any more.
  debug_ipc::NotifyThread notify;
  notify.record.process_koid = exception_info.pid;
  notify.record.thread_koid = exception_info.tid;
  notify.record.state = debug_ipc::ThreadRecord::State::kDead;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadExiting, notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedProcess::OnException(zx::exception exception_token,
                                  zx_exception_info_t exception_info) {
  FXL_DCHECK(exception_info.pid == koid());

  DebuggedThread* thread = GetThread(exception_info.tid);
  if (!thread) {
    FXL_LOG(ERROR) << "Exception on thread " << exception_info.tid << " which we don't know about.";
    return;
  }

  thread->OnException(std::move(exception_token), exception_info);
}

void DebuggedProcess::OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                                     debug_ipc::AddressSpaceReply* reply) {
  std::vector<zx_info_maps_t> map = GetProcessMaps(handle_);
  if (request.address != 0u) {
    for (const auto& entry : map) {
      if (request.address < entry.base)
        continue;
      if (request.address <= (entry.base + entry.size)) {
        reply->map.push_back({entry.name, entry.base, entry.size, entry.depth});
      }
    }
    return;
  }

  size_t ix = 0;
  reply->map.resize(map.size());
  for (const auto& entry : map) {
    reply->map[ix].name = entry.name;
    reply->map[ix].base = entry.base;
    reply->map[ix].size = entry.size;
    reply->map[ix].depth = entry.depth;
    ++ix;
  }
}

void DebuggedProcess::OnModules(debug_ipc::ModulesReply* reply) {
  // Modules can only be read after the debug state is set.
  if (dl_debug_addr_)
    GetModulesForProcess(handle_, dl_debug_addr_, &reply->modules);
}

void DebuggedProcess::OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                                    debug_ipc::WriteMemoryReply* reply) {
  size_t actual = 0;
  reply->status =
      handle_.write_memory(request.address, &request.data[0], request.data.size(), &actual);
  if (reply->status == ZX_OK && actual != request.data.size())
    reply->status = ZX_ERR_IO;  // Convert partial writes to errors.
}

void DebuggedProcess::SuspendAll(bool synchronous, std::vector<uint64_t>* suspended_koids) {
  // We issue the suspension order for all the threads.
  for (auto& [thread_koid, thread] : threads_) {
    bool was_suspended = thread->Suspend(synchronous);
    if (was_suspended) {
      if (suspended_koids)
        suspended_koids->push_back(thread_koid);
    }
  }

  if (!synchronous)
    return;

  // If we want to block, we need to wait on the notification for each thread.
  zx::time deadline = DebuggedThread::DefaultSuspendDeadline();
  for (auto& [thread_koid, thread] : threads_) {
    thread->WaitForSuspension(deadline);
  }
}

void DebuggedProcess::OnStdout(bool close) {
  FXL_DCHECK(stdout_.valid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stdout closed.";
    stdout_.Reset();
    return;
  }

  auto data = ReadSocketInput(&stdout_);
  FXL_DCHECK(!data.empty());
  DEBUG_LOG(Process) << LogPreamble(this) << "Got stdout: " << data.data();
  SendIO(debug_ipc::NotifyIO::Type::kStdout, std::move(data));
}

void DebuggedProcess::OnStderr(bool close) {
  FXL_DCHECK(stderr_.valid());
  if (close) {
    DEBUG_LOG(Process) << LogPreamble(this) << "stderr closed.";
    stderr_.Reset();
    return;
  }

  auto data = ReadSocketInput(&stderr_);
  FXL_DCHECK(!data.empty());
  DEBUG_LOG(Process) << LogPreamble(this) << "Got stderr: " << data.data();
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
    notify.process_koid = koid_;
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
    auto breakpoint =
        std::make_unique<SoftwareBreakpoint>(bp, this, memory_accessor_.get(), address);
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
    auto breakpoint = std::make_unique<HardwareBreakpoint>(bp, this, address, arch_provider_);
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
