// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "process.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fit/function.h>
#include <link.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <cinttypes>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "server.h"

namespace debugserver {
namespace {

constexpr zx_time_t kill_timeout = ZX_MSEC(10 * 1000);

std::unique_ptr<process::ProcessBuilder> CreateProcessBuilder(
    zx_handle_t job, const Argv& argv) {
  FXL_DCHECK(argv.size() > 0);
  zx::job builder_job;
  zx_status_t status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS,
                                           builder_job.reset_and_get_address());
  if (status != ZX_OK)
    return nullptr;

  auto builder =
      std::make_unique<process::ProcessBuilder>(std::move(builder_job));

  builder->AddArgs(argv);
  builder->CloneAll();
  return builder;
}

zx_status_t LoadPath(const char* path, zx_handle_t* vmo) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_status_t status = fdio_get_vmo_clone(fd, vmo);
  close(fd);

  if (status == ZX_OK) {
    if (strlen(path) >= ZX_MAX_NAME_LEN) {
      const char* p = strrchr(path, '/');
      if (p != NULL) {
        path = p + 1;
      }
    }

    zx_object_set_property(*vmo, ZX_PROP_NAME, path, strlen(path));
  }

  return status;
}

bool LoadBinary(process::ProcessBuilder* builder,
                const std::string& binary_path) {
  FXL_DCHECK(builder);

  zx::vmo vmo;
  zx_status_t status =
      LoadPath(binary_path.c_str(), vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not load binary: " << ZxErrorString(status);
    return false;
  }

  builder->LoadVMO(std::move(vmo));
  return true;
}

zx_koid_t GetProcessId(zx_handle_t process) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(process, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info_failed: "
                   << ZxErrorString(status);
    return ZX_KOID_INVALID;
  }

  FXL_DCHECK(info.type == ZX_OBJ_TYPE_PROCESS);
  FXL_DCHECK(info.koid != ZX_KOID_INVALID);

  return info.koid;
}

}  // namespace

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
      memory_(std::shared_ptr<ByteBlock>(new ProcessMemory(this))),
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

void Process::AddStartupHandle(fuchsia::process::HandleInfo handle) {
  extra_handles_.push_back(std::move(handle));
}

bool Process::Initialize() {
  if (IsAttached()) {
    FXL_LOG(ERROR) << "Cannot initialize, already attached to a process";
    return false;
  }

  if (argv_.size() == 0 || argv_[0].size() == 0) {
    FXL_LOG(ERROR) << "No program specified";
    return false;
  }

  zx_handle_t job = server_->job_for_launch();
  if (job == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "No job in which to launch process";
    return false;
  }

  FXL_DCHECK(!builder_);
  FXL_DCHECK(!handle_);
  FXL_DCHECK(!eport_key_);

  zx_status_t status;

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

  FXL_LOG(INFO) << "Initializing process";

  attached_running_ = false;
  // There is no thread map yet.
  thread_map_stale_ = false;

  FXL_LOG(INFO) << "argv: " << ArgvToString(argv_);

  std::string error_message;
  builder_ = CreateProcessBuilder(job, argv_);

  if (!builder_) {
    return false;
  }

  builder_->AddHandles(std::move(extra_handles_));

  if (!LoadBinary(builder_.get(), argv_[0])) {
    goto fail;
  }

  FXL_VLOG(1) << "Binary loaded";

  status = builder_->Prepare(&error_message);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start inferior process: "
                   << ZxErrorString(status) << ": " << error_message;
    goto fail;
  }

  if (!AllocDebugHandle(builder_.get())) {
    goto fail;
  }

  if (!BindExceptionPort()) {
    goto fail;
  }

  FXL_LOG(INFO) << fxl::StringPrintf("Process created: pid %" PRIu64, id_);

  base_address_ = builder_->data().base;
  entry_address_ = builder_->data().entry;

  FXL_DCHECK(IsAttached());

  FXL_LOG(INFO) << fxl::StringPrintf("Process %" PRIu64
                                     ": base load address 0x%" PRIxPTR
                                     ", entry address 0x%" PRIxPTR,
                                     id_, base_address_, entry_address_);

  return true;

fail:
  if (handle_ != ZX_HANDLE_INVALID) {
    CloseDebugHandle();
  }
  if (eport_key_) {
    UnbindExceptionPort();
  }
  id_ = ZX_KOID_INVALID;
  builder_.reset();
  return false;
}

// TODO(dje): Merge common parts with Initialize() after things settle down.

bool Process::Attach(zx_koid_t pid) {
  if (IsAttached()) {
    FXL_LOG(ERROR) << "Cannot attach, already attached to a process";
    return false;
  }
  if (server_->job_for_search() == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Cannot attach, no job for searching processes";
    return false;
  }

  FXL_DCHECK(!builder_);
  FXL_DCHECK(!handle_);
  FXL_DCHECK(!eport_key_);

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

  FXL_LOG(INFO) << "Attaching to process " << pid;

  if (!AllocDebugHandle(pid))
    return false;

  if (!BindExceptionPort()) {
    CloseDebugHandle();
    return false;
  }

  attached_running_ = true;
  set_state(State::kRunning);
  thread_map_stale_ = true;

  FXL_DCHECK(IsAttached());

  FXL_LOG(INFO) << fxl::StringPrintf("Attach complete, pid %" PRIu64, id_);

  return true;
}

bool Process::AllocDebugHandle(process::ProcessBuilder* builder) {
  FXL_DCHECK(builder);

  zx_handle_t process = builder->data().process.get();
  FXL_DCHECK(process);

  // |process| is owned by |builder|.
  // We need our own copy, and ProcessBuilder will give us one, but we need
  // it before we call ProcessBuilder::Start in order to attach to the debugging
  // exception port.
  zx_handle_t debug_process;
  auto status =
      zx_handle_duplicate(process, ZX_RIGHT_SAME_RIGHTS, &debug_process);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_handle_duplicate failed: "
                   << ZxErrorString(status);
    return false;
  }

  id_ = GetProcessId(debug_process);
  handle_ = debug_process;
  return true;
}

bool Process::AllocDebugHandle(zx_koid_t pid) {
  FXL_DCHECK(pid != ZX_KOID_INVALID);
  zx_handle_t job = server_->job_for_search();
  FXL_DCHECK(job != ZX_HANDLE_INVALID);
  auto process = FindProcess(job, pid);
  if (!process.is_valid()) {
    FXL_LOG(ERROR) << "Cannot find process " << pid;
    return false;
  }
  // TODO(dje): It might be useful to use zx::foo throughout. Baby steps.
  auto handle = process.release();

  // TODO(armansito): Check that |handle| has ZX_RIGHT_DEBUG (this seems
  // not to be set by anything at the moment but eventully we should check)?

  // Syscalls shouldn't return ZX_HANDLE_INVALID in the case of ZX_OK.
  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  FXL_VLOG(1) << "Handle " << handle << " obtained for process " << pid;

  handle_ = handle;
  id_ = pid;
  return true;
}

void Process::CloseDebugHandle() {
  FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
  zx_handle_close(handle_);
  handle_ = ZX_HANDLE_INVALID;
}

bool Process::BindExceptionPort() {
  ExceptionPort::Key key = server_->exception_port().Bind(
      handle_, fit::bind_member(this, &Process::OnExceptionOrSignal));
  if (!key)
    return false;
  eport_key_ = key;
  return true;
}

void Process::UnbindExceptionPort() {
  FXL_DCHECK(eport_key_);
  if (!server_->exception_port().Unbind(eport_key_))
    FXL_LOG(WARNING) << "Failed to unbind exception port; ignoring";
  eport_key_ = 0;
}

void Process::RawDetach() {
  // A copy of the handle is kept in ExceptionPort.BindData.
  // We can't close the process handle until we unbind the exception port,
  // so verify it's still open.
  FXL_DCHECK(handle_);
  FXL_DCHECK(IsAttached());

  FXL_LOG(INFO) << "Detaching from process " << id();

  UnbindExceptionPort();
  CloseDebugHandle();
}

bool Process::Detach() {
  if (!IsAttached()) {
    FXL_LOG(ERROR) << "Not attached";
    return false;
  }
  RawDetach();
  Clear();
  return true;
}

bool Process::Start() {
  FXL_DCHECK(builder_);
  FXL_DCHECK(handle_);

  if (state_ != State::kNew) {
    FXL_LOG(ERROR) << "Process already started";
    return false;
  }

  zx_status_t status = builder_->Start(nullptr);
  builder_.reset();

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start inferior process: "
                   << ZxErrorString(status);
    return false;
  }

  set_state(State::kStarting);
  return true;
}

bool Process::Kill() {
  // If the caller wants to flag an error if the process isn't running s/he
  // can, but for our purposes here we're more forgiving.
  switch (state_) {
    case Process::State::kNew:
    case Process::State::kGone:
      FXL_VLOG(1) << "Process is not live";
      return true;
    default:
      break;
  }

  FXL_LOG(INFO) << "Killing process " << id();

  // There's a few issues with sequencing here that we need to consider.
  // - OnProcessExit, called when we receive an exception indicating
  //   the process has exited, will send back a stop reply which we don't want
  // - we don't want to unbind the exception port before killing the process
  //   because we don't want to accidently cause the process to resume before
  //   we kill it
  // - we need the debug handle to kill the process

  FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
  auto status = zx_task_kill(handle_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to kill process: " << ZxErrorString(status);
    return false;
  }

  UnbindExceptionPort();

  zx_signals_t signals;
  // If something goes wrong we don't want to wait forever.
  status = zx_object_wait_one(handle_, ZX_TASK_TERMINATED,
                              zx_deadline_after(kill_timeout), &signals);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error waiting for process to die, ignoring: "
                   << ZxErrorString(status);
  } else {
    FXL_DCHECK(signals & ZX_TASK_TERMINATED);
  }

  CloseDebugHandle();

  Clear();
  return true;
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

  id_ = ZX_KOID_INVALID;
  base_address_ = 0;
  entry_address_ = 0;
  attached_running_ = false;

  dso_free_list(dsos_);
  dsos_ = nullptr;
  dsos_build_failed_ = false;

  builder_.reset();

  // The process may just exited or whatever. Force the state to kGone.
  set_state(State::kGone);
}

bool Process::IsLive() const {
  return state_ != State::kNew && state_ != State::kGone;
}

bool Process::IsAttached() const {
  if (eport_key_) {
    FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
    return true;
  } else {
    FXL_DCHECK(handle_ == ZX_HANDLE_INVALID);
    return false;
  }
}

void Process::EnsureThreadMapFresh() {
  if (thread_map_stale_) {
    RefreshAllThreads();
  }
}

Thread* Process::FindThreadById(zx_koid_t thread_id) {
  FXL_DCHECK(handle_);
  if (thread_id == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Invalid thread ID given: " << thread_id;
    return nullptr;
  }

  EnsureThreadMapFresh();

  const auto iter = threads_.find(thread_id);
  if (iter != threads_.end()) {
    Thread* thread = iter->second.get();
    if (thread->state() == Thread::State::kGone) {
      FXL_VLOG(1) << "FindThreadById: Thread " << thread->GetDebugName()
                  << " is gone";
      return nullptr;
    }
    return thread;
  }

  // Try to get a debug capable handle to the child of the current process with
  // a kernel object ID that matches |thread_id|.
  zx_handle_t thread_handle;
  zx_status_t status = zx_object_get_child(
      handle_, thread_id, ZX_RIGHT_SAME_RIGHTS, &thread_handle);
  if (status != ZX_OK) {
    // If the process just exited then the thread will be gone. So this is
    // just a debug message, not a warning or error.
    FXL_VLOG(1) << "Could not obtain a debug handle to thread " << thread_id
                << ": " << ZxErrorString(status);
    return nullptr;
  }

  Thread* thread = new Thread(this, thread_handle, thread_id);
  threads_[thread_id] = std::unique_ptr<Thread>(thread);
  return thread;
}

Thread* Process::PickOneThread() {
  EnsureThreadMapFresh();

  if (threads_.empty())
    return nullptr;

  return threads_.begin()->second.get();
}

bool Process::RefreshAllThreads() {
  FXL_DCHECK(handle_);

  // First get the thread count so that we can allocate an appropriately sized
  // buffer. This is racy but unless the caller stops all threads that's just
  // the way things are.
  size_t num_threads;
  zx_status_t status = zx_object_get_info(handle_, ZX_INFO_PROCESS_THREADS,
                                          nullptr, 0, nullptr, &num_threads);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get process thread info (#threads): "
                   << ZxErrorString(status);
    return false;
  }

  auto buffer_size = num_threads * sizeof(zx_koid_t);
  auto koids = std::make_unique<zx_koid_t[]>(num_threads);
  size_t records_read;
  status = zx_object_get_info(handle_, ZX_INFO_PROCESS_THREADS, koids.get(),
                              buffer_size, &records_read, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get process thread info: "
                   << ZxErrorString(status);
    return false;
  }

  FXL_DCHECK(records_read == num_threads);

  ThreadMap new_threads;
  for (size_t i = 0; i < num_threads; ++i) {
    zx_koid_t thread_id = koids[i];
    zx_handle_t thread_handle = ZX_HANDLE_INVALID;
    status = zx_object_get_child(handle_, thread_id, ZX_RIGHT_SAME_RIGHTS,
                                 &thread_handle);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not obtain a debug handle to thread: "
                     << ZxErrorString(status);
      continue;
    }
    new_threads[thread_id] =
        std::make_unique<Thread>(this, thread_handle, thread_id);
  }

  // Just clear the existing list and repopulate it.
  threads_ = std::move(new_threads);
  thread_map_stale_ = false;

  return true;
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

void Process::TryBuildLoadedDsosList(Thread* thread, bool check_ldso_bkpt) {
  FXL_DCHECK(dsos_ == nullptr);

  FXL_VLOG(2) << "Building dso list";

  uintptr_t debug_addr;
  zx_handle_t process_handle = thread->process()->handle();
  zx_status_t status =
      zx_object_get_property(process_handle, ZX_PROP_PROCESS_DEBUG_ADDR,
                             &debug_addr, sizeof(debug_addr));
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "zx_object_get_property failed, unable to fetch dso list: "
        << ZxErrorString(status);
    return;
  }

  struct r_debug debug;
  if (!ReadMemory(debug_addr, &debug, sizeof(debug))) {
    FXL_VLOG(2) << "unable to read _dl_debug_addr";
    // Don't set dsos_build_failed_ here, it may be too early to try.
    return;
  }

  // Since we could, theoretically, stop in the dynamic linker before we get
  // that far check to see if it has been filled in.
  // TODO(dje): Document our test in dynlink.c.
  if (debug.r_version == 0) {
    FXL_VLOG(2) << "debug.r_version is 0";
    // Don't set dsos_build_failed_ here, it may be too early to try.
    return;
  }

  if (check_ldso_bkpt) {
    FXL_DCHECK(thread);
    bool success = thread->registers()->RefreshGeneralRegisters();
    FXL_DCHECK(success);
    zx_vaddr_t pc = thread->registers()->GetPC();
    // TODO(dje): -1: adjust_pc_after_break
    if (pc - 1 != debug.r_brk) {
      FXL_VLOG(2) << "not stopped at dynamic linker debug breakpoint";
      return;
    }
  } else {
    FXL_DCHECK(!thread);
  }

  auto lmap_vaddr = reinterpret_cast<zx_vaddr_t>(debug.r_map);
  dsos_ = dso_fetch_list(memory_, lmap_vaddr, "app");
  // We should have fetched at least one since this is not called until the
  // dl_debug_state breakpoint is hit.
  if (dsos_ == nullptr) {
    // Don't keep trying.
    FXL_VLOG(2) << "dso_fetch_list failed";
    dsos_build_failed_ = true;
  } else {
    dso_vlog_list(dsos_);
    // This may already be false, but set it any for documentation purposes.
    dsos_build_failed_ = false;
  }
}

void Process::OnExceptionOrSignal(const zx_port_packet_t& packet,
                                  const zx_exception_context_t& context) {
  // Process exit is sent as a regular signal.
  if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
    FXL_VLOG(1) << "Received ZX_PKT_TYPE_SIGNAL_ONE, trigger 0x" << std::hex
                << packet.signal.trigger;
    if (packet.signal.trigger & ZX_TASK_TERMINATED) {
      set_state(Process::State::kGone);
      delegate_->OnProcessExit(this);
      if (!Detach()) {
        // This is not a fatal error, just log it.
        FXL_LOG(ERROR) << "Unexpected failure to detach (already detached)";
        Clear();
      }
    }
    return;
  }

  zx_excp_type_t type = static_cast<zx_excp_type_t>(packet.type);
  zx_koid_t tid = packet.exception.tid;
  Thread* thread = nullptr;
  if (tid != ZX_KOID_INVALID) {
    thread = FindThreadById(tid);
    // TODO(dje): handle process exit
  }

  // Finding the load address of the main executable requires a few steps.
  // It's not loaded until the first time we hit the _dl_debug_state
  // breakpoint. For now gdb sets that breakpoint. What we do is watch for
  // s/w breakpoint exceptions.
  if (type == ZX_EXCP_SW_BREAKPOINT) {
    FXL_DCHECK(thread);
    if (!DsosLoaded() && !dsos_build_failed_)
      TryBuildLoadedDsosList(thread, true);
  }

  // |type| could either map to an architectural exception or Zircon-defined
  // synthetic exceptions.
  if (ZX_EXCP_IS_ARCH(type)) {
    FXL_DCHECK(thread);
    thread->OnException(type, context);
    delegate_->OnArchitecturalException(this, thread, type, context);
    return;
  }

  switch (type) {
    case ZX_EXCP_THREAD_STARTING:
      FXL_DCHECK(thread);
      FXL_DCHECK(thread->state() == Thread::State::kNew);
      FXL_VLOG(1) << "Received ZX_EXCP_THREAD_STARTING exception for thread "
                  << thread->GetName();
      thread->OnException(type, context);
      delegate_->OnThreadStarting(this, thread, context);
      break;
    case ZX_EXCP_THREAD_EXITING:
      // If the process also exited, then the thread may be gone.
      if (thread) {
        FXL_VLOG(1) << "Received ZX_EXCP_THREAD_EXITING exception for thread "
                    << tid << ", " << thread->GetName();
        thread->OnException(type, context);
        delegate_->OnThreadExiting(this, thread, context);
      } else {
        FXL_VLOG(1) << "Received ZX_EXCP_THREAD_EXITING exception for thread "
                    << tid;
      }
      break;
    case ZX_EXCP_POLICY_ERROR:
      FXL_DCHECK(thread);
      FXL_VLOG(1) << "Received ZX_EXCP_POLICY_ERROR exception for thread "
                  << thread->GetName();
      thread->OnException(type, context);
      delegate_->OnSyntheticException(this, thread, type, context);
      break;
    default:
      FXL_LOG(ERROR) << "Ignoring unrecognized synthetic exception for thread "
                     << tid << ": " << type;
      break;
  }
}

int Process::ExitCode() {
  FXL_DCHECK(state_ == State::kGone);
  zx_info_process_t info;
  auto status = zx_object_get_info(handle(), ZX_INFO_PROCESS, &info,
                                   sizeof(info), NULL, NULL);
  if (status == ZX_OK) {
    FXL_LOG(INFO) << "Process exited with code " << info.return_code;
    return info.return_code;
  } else {
    FXL_LOG(ERROR) << "Error getting process exit code: "
                   << ZxErrorString(status);
    return -1;
  }
}

const dsoinfo_t* Process::GetExecDso() {
  return dso_get_main_exec(dsos_);
}

dsoinfo_t* Process::LookupDso(zx_vaddr_t pc) const {
  return dso_lookup(dsos_, pc);
}

}  // namespace debugserver
