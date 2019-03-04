// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <link.h>
#include <cinttypes>

#include <lib/fdio/io.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "garnet/lib/debugger_utils/breakpoints.h"
#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/processes.h"
#include "garnet/lib/debugger_utils/util.h"

#include "process.h"
#include "server.h"

namespace inferior_control {

// static
const char* Process::StateName(Process::State state) {
#define CASE_TO_STR(x)    \
  case Process::State::x: \
    return #x
  switch (state) {
    CASE_TO_STR(kNew);
    CASE_TO_STR(kStarting);
    CASE_TO_STR(kRunning);
    CASE_TO_STR(kGone);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

Process::Process(Server* server, Delegate* delegate)
    : server_(server),
      delegate_(delegate),
      memory_(
          std::shared_ptr<debugger_utils::ByteBlock>(new ProcessMemory(this))),
      breakpoints_(this) {
  FXL_DCHECK(server_);
  FXL_DCHECK(delegate_);
}

Process::~Process() {
  // If we're still attached then either kill the process if we
  // started it or detach if we attached to it after it was running.
  if (attached_running_) {
    RawDetach();
  } else {
    if (!Kill()) {
      // Paranoia: Still need to detach before we can call Clear().
      RawDetach();
    }
  }
  Clear();
}

std::string Process::GetName() const {
  return fxl::StringPrintf("%" PRId64, id());
}

bool Process::InitializeFromBuilder(
    std::unique_ptr<process::ProcessBuilder> builder) {
  std::string error_message;
  zx_status_t status = builder->Prepare(&error_message);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to initialize process: "
                   << debugger_utils::ZxErrorString(status) << ": "
                   << error_message;
    return false;
  }

  base_address_ = builder->data().base;
  entry_address_ = builder->data().entry;

  zx::process process;
  status = builder->data().process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  FXL_DCHECK(status == ZX_OK);

  if (!AttachToNew(std::move(process),
                   [builder = std::move(builder)](Process* p) -> zx_status_t {
        return builder->Start(nullptr);
      })) {
    FXL_LOG(ERROR) << "Unable to attach to inferior process";
    return false;
  }

  return true;
}

bool Process::AttachToNew(zx::process process, StartCallback start_callback) {
  FXL_DCHECK(process);
  FXL_DCHECK(start_callback);
  if (!AttachWorker(std::move(process), false)) {
    return false;
  }
  start_callback_ = std::move(start_callback);
  return true;
}

bool Process::AttachToRunning(zx::process process) {
  FXL_DCHECK(process);
  return AttachWorker(std::move(process), true);
}

bool Process::AttachWorker(zx::process process, bool attach_running) {
  FXL_DCHECK(process);

  if (IsAttached()) {
    FXL_LOG(ERROR) << "Cannot initialize, already attached to a process";
    return false;
  }
  FXL_DCHECK(!eport_bound_);

  // The Process object survives run-after-run. Switch Gone back to New.
  switch (state_) {
    case State::kNew:
      break;
    case State::kGone:
      set_state(State::kNew);
      break;
    default:
      // Shouldn't get here if process is currently live.
      FXL_DCHECK(false);
  }

  zx_koid_t pid = debugger_utils::GetKoid(process);

  if (attach_running) {
    FXL_LOG(INFO) << "Attaching to process " << pid;
  } else {
    FXL_LOG(INFO) << "Attaching to new process " << pid;
  }

  FXL_DCHECK(!process_);
  process_ = std::move(process);
  id_ = pid;

  if (!BindExceptionPort()) {
    goto fail;
  }
  FXL_DCHECK(IsAttached());

  if (!attach_running) {
    if (!SetLdsoDebugTrigger()) {
      goto fail;
    }
  } else {
    set_state(State::kRunning);
    // TODO(dje): Update ldso state (debug_addr_property_, etc.).
  }

  attached_running_ = attach_running;
  if (attach_running) {
    thread_map_stale_ = true;
  } else {
    // There is no thread map yet.
    thread_map_stale_ = false;
  }

  FXL_VLOG(4) << "Attach complete, pid " << id_;

  return true;

fail:
  if (eport_bound_) {
    UnbindExceptionPort();
  }
  process_.reset();
  id_ = ZX_KOID_INVALID;
  return false;
}

bool Process::BindExceptionPort() {
  if (!server_->exception_port().Bind(process_, id_)) {
    FXL_LOG(ERROR) << "Unable to bind process " << id_ << " to exception port";
    return false;
  }
  FXL_VLOG(2) << "Process " << id_ << " bound to exception port";
  eport_bound_ = true;
  return true;
}

void Process::UnbindExceptionPort() {
  FXL_DCHECK(eport_bound_);
  FXL_DCHECK(process_);
  __UNUSED bool success = server_->exception_port().Unbind(process_, id_);
  FXL_DCHECK(success);
  eport_bound_ = false;
}

void Process::RawDetach() {
  // We can't close the process handle until we unbind the exception port,
  // so verify it's still open.
  FXL_DCHECK(process_);
  FXL_DCHECK(IsAttached());

  FXL_LOG(INFO) << "Detaching from process " << id();

  UnbindExceptionPort();
  process_.reset();
}

bool Process::Detach() {
  if (!IsAttached()) {
    FXL_LOG(ERROR) << "Not attached";
    return false;
  }

  // If detaching from an inferior we started, and we haven't seen the ld.so
  // breakpoint yet, then remove it. Otherwise the inferior will crash when
  // it hits the breakpoint.
  // N.B. In this situation it is the caller's responsibility to only call
  // us when the inferior is stopped. Typically this happens when processing
  // the THREAD_STARTING exception for the initial thread.
  if (!attached_running_ && !ldso_debug_data_has_initialized_) {
    zx_vaddr_t debug_addr;
    if (!GetDebugAddrProperty(&debug_addr)) {
      return false;
    }
    if (debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET) {
      // TODO(ZX-3627): Use official value when available.
      constexpr zx_vaddr_t ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET_DISABLED = 2u;
      if (!SetDebugAddrProperty(ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET_DISABLED)) {
        return false;
      }
    }
  }

  RawDetach();
  Clear();
  return true;
}

bool Process::Start() {
  FXL_DCHECK(process_);

  if (state_ != State::kNew) {
    FXL_LOG(ERROR) << "Process already started";
    return false;
  }

  FXL_DCHECK(start_callback_);
  zx_status_t status = start_callback_(this);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start inferior process: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  start_callback_ = StartCallback{};
  set_state(State::kStarting);
  return true;
}

bool Process::Kill() {
  // If the caller wants to flag an error if the process isn't running s/he
  // can, but for our purposes here we're more forgiving.
  switch (state_) {
    case Process::State::kNew:
    case Process::State::kGone:
      FXL_VLOG(2) << "Process is not live";
      return true;
    default:
      break;
  }

  FXL_LOG(INFO) << "Killing process " << id();

  // Request the process be killed. Cleanup is handled by the async loop
  // when it receives ZX_PROCESS_TERMINATED.

  FXL_DCHECK(process_);
  auto status = process_.kill();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Kill request failed for process " << id() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  return true;
}

bool Process::RequestSuspend() {
  FXL_DCHECK(!suspend_token_);

  switch (state_) {
    case Process::State::kGone:
      FXL_VLOG(2) << "Process " << id() << " is not live";
      return false;
    default:
      break;
  }

  FXL_LOG(INFO) << "Suspending process " << id();

  FXL_DCHECK(process_);
  auto status = process_.suspend(&suspend_token_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to suspend process " << id() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  return true;
}

void Process::ResumeFromSuspension() {
  FXL_CHECK(suspend_token_);
  FXL_LOG(INFO) << "Resuming process " << id();
  suspend_token_.reset();
}

void Process::set_state(State new_state) {
  switch (new_state) {
    case State::kNew:
      FXL_DCHECK(state_ == State::kGone);
      break;
    case State::kStarting:
      FXL_DCHECK(state_ == State::kNew);
      break;
    case State::kRunning:
      FXL_DCHECK(state_ == State::kNew || state_ == State::kStarting);
      break;
    case State::kGone:
      break;
    default:
      FXL_DCHECK(false);
  }
  state_ = new_state;
}

void Process::Clear() {
  // The process must already be fully detached from.
  FXL_DCHECK(!IsAttached());

  threads_.clear();
  thread_map_stale_ = false;

  // Note: |id_| is intentionally not reset here.
  process_.reset();

  debug_addr_property_ = 0;
  ldso_debug_data_has_initialized_ = false;
  ldso_debug_break_addr_ = 0;
  ldso_debug_map_addr_ = 0;

  base_address_ = 0;
  entry_address_ = 0;
  attached_running_ = false;
  start_callback_ = StartCallback{};

  dso_free_list(dsos_);
  dsos_ = nullptr;
  dsos_build_failed_ = false;

  // The process may have just exited or whatever. Force the state to kGone.
  set_state(State::kGone);
}

Thread* Process::AddThread(zx_handle_t thread_handle, zx_koid_t thread_id) {
  Thread* thread = new Thread(this, thread_handle, thread_id);
  threads_[thread_id] = std::unique_ptr<Thread>(thread);

  // Begin watching for thread signals we care about.
  // There's no need for an explicit cancellation, that'll happen when the
  // thread's handle is closed.
  server_->WaitAsync(thread);

  return thread;
}

bool Process::IsLive() const {
  return state_ != State::kNew && state_ != State::kGone;
}

bool Process::IsAttached() const {
  if (eport_bound_) {
    FXL_DCHECK(process_);
    return true;
  } else {
    FXL_DCHECK(!process_);
    return false;
  }
}

void Process::EnsureThreadMapFresh() {
  if (thread_map_stale_) {
    RefreshAllThreads();
  }
}

Thread* Process::FindThreadById(zx_koid_t thread_id) {
  if (thread_id == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Invalid thread ID given: " << thread_id;
    return nullptr;
  }

  // If process is dead all its threads have been removed.
  if (state_ == State::kGone) {
    FXL_VLOG(2) << "FindThreadById: Process " << id_ << " is gone, thread "
                << thread_id << " is gone";
    return nullptr;
  }

  FXL_DCHECK(process_);
  EnsureThreadMapFresh();

  const auto iter = threads_.find(thread_id);
  if (iter != threads_.end()) {
    Thread* thread = iter->second.get();
    if (thread->state() == Thread::State::kGone) {
      FXL_VLOG(2) << "FindThreadById: Thread " << thread->GetDebugName()
                  << " is gone";
      return nullptr;
    }
    return thread;
  }

  // Try to get a debug capable handle to the child of the current process with
  // a kernel object ID that matches |thread_id|.
  zx::thread thread;
  zx_status_t status = process_.get_child(
      thread_id, ZX_RIGHT_SAME_RIGHTS, &thread);
  if (status != ZX_OK) {
    // If the process just exited then the thread will be gone. So this is
    // just a debug message, not a warning or error.
    FXL_VLOG(2) << "Could not obtain a debug handle to thread " << thread_id
                << ": " << debugger_utils::ZxErrorString(status);
    return nullptr;
  }

  return AddThread(thread.release(), thread_id);
}

Thread* Process::PickOneThread() {
  EnsureThreadMapFresh();

  if (threads_.empty())
    return nullptr;

  return threads_.begin()->second.get();
}

void Process::RefreshAllThreads() {
  FXL_DCHECK(process_);

  std::vector<zx_koid_t> threads;
  size_t num_available_threads;

  __UNUSED zx_status_t status =
      debugger_utils::GetProcessThreadKoids(process_, kRefreshThreadsTryCount,
          kNumExtraRefreshThreads, &threads, &num_available_threads);
  // The only way this can fail is if we have a bug (or the kernel runs out
  // of memory, but we don't try to cope with that case).
  // TODO(dje): Verify the handle we are given has sufficient rights.
  FXL_DCHECK(status == ZX_OK);

  // The heuristic we use to collect all threads is sufficient that this
  // will never fail in practice. If it does we need to adjust it.
  FXL_DCHECK(threads.size() == num_available_threads);

  for (auto tid : threads) {
    if (threads_.find(tid) != threads_.end()) {
      // We already have this thread.
      continue;
    }
    zx::thread thread_obj;
    status = process_.get_child(tid, ZX_RIGHT_SAME_RIGHTS, &thread_obj);
    // The only way this can otherwise fail is if we have a bug.
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
    if (status == ZX_ERR_NOT_FOUND) {
      // Thread died in the interim.
      continue;
    }
    __UNUSED Thread* thread = AddThread(thread_obj.release(), tid);
  }

  thread_map_stale_ = false;
}

void Process::ForEachThread(const ThreadCallback& callback) {
  EnsureThreadMapFresh();

  for (const auto& iter : threads_)
    callback(iter.second.get());
}

void Process::ForEachLiveThread(const ThreadCallback& callback) {
  EnsureThreadMapFresh();

  for (const auto& iter : threads_) {
    Thread* thread = iter.second.get();
    if (thread->state() != Thread::State::kGone)
      callback(thread);
  }
}

bool Process::ReadMemory(uintptr_t address, void* out_buffer, size_t length) {
  return memory_->Read(address, out_buffer, length);
}

bool Process::WriteMemory(uintptr_t address, const void* data, size_t length) {
  return memory_->Write(address, data, length);
}

bool Process::GetDebugAddrProperty(zx_vaddr_t* out_debug_addr) {
  zx_status_t status =
      process_.get_property(ZX_PROP_PROCESS_DEBUG_ADDR,
                            out_debug_addr, sizeof(*out_debug_addr));
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "zx_object_get_property failed, unable to fetch DEBUG_ADDR: "
        << debugger_utils::ZxErrorString(status);
    return false;
  }
  return true;
}

bool Process::SetDebugAddrProperty(zx_vaddr_t debug_addr) {
  zx_status_t status =
      process_.set_property(ZX_PROP_PROCESS_DEBUG_ADDR,
                            &debug_addr, sizeof(debug_addr));
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "zx_object_set_property failed, unable to set DEBUG_ADDR: "
        << debugger_utils::ZxErrorString(status);
    return false;
  }
  return true;
}

bool Process::SetLdsoDebugTrigger() {
  return SetDebugAddrProperty(ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET);
}

zx_vaddr_t Process::GetDebugAddr() {
  if (debug_addr_property_ != 0)
    return debug_addr_property_;

  zx_vaddr_t debug_addr;
  if (!GetDebugAddrProperty(&debug_addr)) {
    return 0;
  }

  // Since we could, theoretically, stop in the dynamic linker before we get
  // that far check to see if it has been filled in.
  if (debug_addr == 0 ||
      debug_addr == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET) {
    FXL_VLOG(4) << "Ld.so hasn't loaded symbols yet";
    return 0;
  }

  debug_addr_property_ = debug_addr;
  return debug_addr;
}

bool Process::CheckLdsoDebugAddrBreak() {
  FXL_DCHECK(!ldso_debug_data_has_initialized_);
  FXL_DCHECK(debug_addr_property_ != 0);

  // The address isn't recorded in r_debug like the "standard" dynamic linker
  // breakpoint so we have to use a heuristic. The heuristic is reasonably
  // robust: If this is the first s/w breakpoint we've seen after
  // |r_debug.r_version| becomes non-zero, then we're stopped at the
  // ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET breakpoint. We have to assume
  // the user doesn't stop ld.so before it issues its s/w breakpoint.
  // This assumption can be removed when we know the address of that s/w
  // breakpoint instruction.

  struct r_debug debug;
  if (!ReadMemory(debug_addr_property_, &debug, sizeof(debug))) {
    FXL_LOG(ERROR) << "unable to read _dl_debug_addr";
    return false;
  }
  if (debug.r_version == 0) {
    FXL_VLOG(4) << "debug.r_version is 0";
    return false;
  }

  if (debug.r_brk == 0 || debug.r_map == 0) {
    // Sigh. We could have stopped after r_version was set but before
    // these were set. Technically, this could also happen due to an
    // incompatible ld.so change or even a bug, but these are rare enough
    // that we don't consider them here.
    FXL_VLOG(4) << "debug.r_brk or r_map is 0";
    return false;
  }

  ldso_debug_break_addr_ = debug.r_brk;
  ldso_debug_map_addr_ = reinterpret_cast<zx_vaddr_t>(debug.r_map);
  ldso_debug_data_has_initialized_ = true;
  return true;
}

void Process::TryBuildLoadedDsosList(Thread* thread) {
  FXL_DCHECK(thread);
  FXL_DCHECK(dsos_ == nullptr);
  FXL_DCHECK(ldso_debug_map_addr_ != 0);

  FXL_VLOG(4) << "Building dso list";

  dsos_ = dso_fetch_list(memory_, ldso_debug_map_addr_, "app");
  // We should have fetched at least one since this is not called until the
  // dl_debug_state (or debug_break) breakpoint is hit.
  if (dsos_ == nullptr) {
    // Don't keep trying.
    FXL_VLOG(4) << "dso_fetch_list failed";
    dsos_build_failed_ = true;
  } else {
    dso_vlog_list(dsos_);
    // This may already be false, but set it any for documentation purposes.
    dsos_build_failed_ = false;
  }
}

bool Process::CheckDsosList(Thread* thread) {
  // TODO(dje): dlopen
  if (DsosLoaded() || dsos_build_failed_) {
    return false;
  }

  // There are a few issues to consider here, we handle them in order of
  // potential occurrence.

  // Has the dynamic linker sufficiently initialized yet?
  zx_vaddr_t debug_addr = GetDebugAddr();
  if (debug_addr == 0) {
    return false;
  }

  // Are we stopped at the ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET breakpoint?
  if (!ldso_debug_data_has_initialized_) {
    if (!CheckLdsoDebugAddrBreak()) {
      return false;
    }
    FXL_DCHECK(ldso_debug_data_has_initialized_);
    TryBuildLoadedDsosList(thread);
    return true;
  }

  // Are we stopped at the "standard" dynamic linker breakpoint?
  // Note that this is (currently) a different location than the
  // ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET, but fortunately we know its location.
  bool success = thread->registers()->RefreshGeneralRegisters();
  FXL_DCHECK(success);
  zx_vaddr_t pc = thread->registers()->GetPC();
  pc = debugger_utils::DecrementPcAfterBreak(pc);
  if (pc != ldso_debug_break_addr_) {
    FXL_VLOG(4) << "not stopped at dynamic linker debug breakpoint";
    return false;
  }
  TryBuildLoadedDsosList(thread);
  return true;
}

const debugger_utils::dsoinfo_t* Process::GetExecDso() {
  return dso_get_main_exec(dsos_);
}

debugger_utils::dsoinfo_t* Process::LookupDso(zx_vaddr_t pc) const {
  return dso_lookup(dsos_, pc);
}

void Process::OnTermination() {
  set_state(Process::State::kGone);
  RecordReturnCode();
  delegate_->OnProcessTermination(this);

  // After detaching the process's state is cleared.
  zx_koid_t pid = id_;

  if (!Detach()) {
    // This is not a fatal error, just log it.
    FXL_LOG(ERROR) << "Unexpected failure to detach (already detached)";
    // The process is still dead, make sure it's fully marked so.
    Clear();
  }

  FXL_LOG(INFO) << "Process " << pid << " now marked as dead";
}

void Process::RecordReturnCode() {
  FXL_DCHECK(state_ == State::kGone);
  zx_status_t status = debugger_utils::GetProcessReturnCode(process_.get(),
                                                            &return_code_);
  if (status == ZX_OK) {
    return_code_is_set_ = true;
    FXL_VLOG(4) << "Process " << GetName() << " exited with return code "
                << return_code_;
  } else {
    FXL_LOG(ERROR) << "Error getting process exit code: "
                   << debugger_utils::ZxErrorString(status);
  }
}

void Process::Dump() {
  EnsureThreadMapFresh();
  FXL_LOG(INFO) << "Dump of threads for process " << id_;

  ForEachLiveThread([](Thread* thread) {
    thread->Dump();
  });
}

}  // namespace inferior_control
