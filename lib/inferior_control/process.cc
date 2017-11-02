// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "process.h"

#include <cinttypes>
#include <link.h>

#include <launchpad/vmo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <fdio/io.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "server.h"

namespace debugserver {
namespace {

constexpr zx_time_t kill_timeout = ZX_MSEC(10 * 1000);

bool SetupLaunchpad(launchpad_t** out_lp, const util::Argv& argv) {
  FXL_DCHECK(out_lp);
  FXL_DCHECK(argv.size() > 0);

  // Construct the argument array.
  const char* c_args[argv.size()];
  for (size_t i = 0; i < argv.size(); ++i)
    c_args[i] = argv[i].c_str();
  const char* name = util::basename(c_args[0]);

  launchpad_t* lp = nullptr;
  zx_status_t status = launchpad_create(0u, name, &lp);
  if (status != ZX_OK)
    goto fail;

  status = launchpad_set_args(lp, argv.size(), c_args);
  if (status != ZX_OK)
    goto fail;

  status = launchpad_add_vdso_vmo(lp);
  if (status != ZX_OK)
    goto fail;

  // Clone root, cwd, stdio, and environ.
  launchpad_clone(lp, LP_CLONE_FDIO_ALL | LP_CLONE_ENVIRON);

  *out_lp = lp;
  return true;

fail:
  FXL_LOG(ERROR) << "Process setup failed: " << util::ZxErrorString(status);
  if (lp)
    launchpad_destroy(lp);
  return false;
}

bool LoadBinary(launchpad_t* lp, const std::string& binary_path) {
  FXL_DCHECK(lp);

  zx_handle_t vmo;
  zx_status_t status = launchpad_vmo_from_file(binary_path.c_str(), &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not load binary: " << util::ZxErrorString(status);
    return false;
  }

  status = launchpad_elf_load(lp, vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not load binary: " << util::ZxErrorString(status);
    return false;
  }

  status = launchpad_load_vdso(lp, ZX_HANDLE_INVALID);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not load vDSO: " << util::ZxErrorString(status);
    return false;
  }

  return true;
}

zx_koid_t GetProcessId(launchpad_t* lp) {
  FXL_DCHECK(lp);

  // We use the zx_object_get_child syscall to obtain a debug-capable handle
  // to the process. For processes, the syscall expect the ID of the underlying
  // kernel object (koid, also passing for process id in Zircon).
  zx_handle_t process_handle = launchpad_get_process_handle(lp);
  FXL_DCHECK(process_handle);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(process_handle, ZX_INFO_HANDLE_BASIC, &info,
                         sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info_failed: "
                   << util::ZxErrorString(status);
    return ZX_KOID_INVALID;
  }

  FXL_DCHECK(info.type == ZX_OBJ_TYPE_PROCESS);

  return info.koid;
}

zx_handle_t GetProcessDebugHandle(zx_koid_t pid) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = zx_object_get_child(ZX_HANDLE_INVALID, pid,
                                           ZX_RIGHT_SAME_RIGHTS, &handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_child failed: "
                   << util::ZxErrorString(status);
    return ZX_HANDLE_INVALID;
  }

  // TODO(armansito): Check that |handle| has ZX_RIGHT_DEBUG (this seems
  // not to be set by anything at the moment but eventully we should check)?

  // Syscalls shouldn't return ZX_HANDLE_INVALID in the case of ZX_OK.
  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  FXL_VLOG(1) << "Handle " << handle << " obtained for process " << pid;

  return handle;
}

}  // namespace

// static
const char* Process::StateName(Process::State state) {
#define CASE_TO_STR(x)     \
  case Process::State::x:  \
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
      memory_(std::shared_ptr<util::ByteBlock>(new ProcessMemory(this))),
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

bool Process::Initialize() {
  FXL_DCHECK(!launchpad_);
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

  if (argv_.size() == 0 || argv_[0].size() == 0) {
    FXL_LOG(ERROR) << "No program specified";
    return false;
  }

  FXL_LOG(INFO) << "argv: " << util::ArgvToString(argv_);

  if (!SetupLaunchpad(&launchpad_, argv_))
    return false;

  FXL_LOG(INFO) << "Process setup complete";

  if (!LoadBinary(launchpad_, argv_[0]))
    goto fail;

  FXL_VLOG(1) << "Binary loaded";

  // Initialize the PID.
  id_ = GetProcessId(launchpad_);
  FXL_DCHECK(id_ != ZX_KOID_INVALID);

  status = launchpad_get_base_address(launchpad_, &base_address_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Failed to obtain the dynamic linker base address for process: "
        << util::ZxErrorString(status);
    goto fail;
  }

  status = launchpad_get_entry_address(launchpad_, &entry_address_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Failed to obtain the dynamic linker entry address for process: "
        << util::ZxErrorString(status);
    goto fail;
  }

  FXL_LOG(INFO) << "Obtained base load address: "
                << fxl::StringPrintf("0x%" PRIxPTR, base_address_)
                << ", entry address: "
                << fxl::StringPrintf("0x%" PRIxPTR, entry_address_);

  return true;

fail:
  id_ = ZX_KOID_INVALID;
  launchpad_destroy(launchpad_);
  launchpad_ = nullptr;
  return false;
}

// TODO(dje): Merge common parts with Initialize() after things settle down.

bool Process::Initialize(zx_koid_t pid) {
  FXL_DCHECK(!launchpad_);
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

  FXL_LOG(INFO) << "Initializing process";

  attached_running_ = true;
  id_ = pid;

  FXL_LOG(INFO) << "Process setup complete";

  return true;
}

bool Process::AllocDebugHandle() {
  FXL_DCHECK(handle_ == ZX_HANDLE_INVALID);
  auto handle = GetProcessDebugHandle(id_);
  if (handle == ZX_HANDLE_INVALID)
    return false;
  handle_ = handle;
  return true;
}

void Process::CloseDebugHandle() {
  zx_handle_close(handle_);
  handle_ = ZX_HANDLE_INVALID;
}

bool Process::BindExceptionPort() {
  ExceptionPort::Key key = server_->exception_port()->Bind(
    handle_,
    std::bind(&Process::OnException, this, std::placeholders::_1,
              std::placeholders::_2));
  if (!key)
    return false;
  eport_key_ = key;
  return true;
}

void Process::UnbindExceptionPort() {
  FXL_DCHECK(eport_key_);
  if (!server_->exception_port()->Unbind(eport_key_))
    FXL_LOG(WARNING) << "Failed to unbind exception port; ignoring";
  eport_key_ = 0;
}

bool Process::Attach() {
  if (IsAttached()) {
    FXL_LOG(ERROR) << "Cannot attach an already attached process";
    return false;
  }

  FXL_LOG(INFO) << "Attaching to process " << id();

  if (!AllocDebugHandle())
    return false;

  if (!BindExceptionPort()) {
    CloseDebugHandle();
    return false;
  }

  if (attached_running_) {
    set_state(State::kRunning);
    thread_map_stale_ = true;
  }

  return true;
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
  FXL_DCHECK(launchpad_);
  FXL_DCHECK(handle_);

  if (state_ != State::kNew) {
    FXL_LOG(ERROR) << "Process already started";
    return false;
  }

  // launchpad_go returns a dup of the process handle (owned by
  // |launchpad_|), where the original handle is given to the child. We have to
  // close the dup handle to avoid leaking it.
  zx_handle_t dup_handle;
  zx_status_t status = launchpad_go(launchpad_, &dup_handle, nullptr);

  // Launchpad is no longer needed after launchpad_go returns.
  launchpad_ = nullptr;

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start inferior process: "
                   << util::ZxErrorString(status);
    return false;
  }
  zx_handle_close(dup_handle);

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
    FXL_LOG(ERROR) << "Failed to kill process: " << util::ZxErrorString(status);
    return false;
  }

  UnbindExceptionPort();

  zx_signals_t signals;
  // If something goes wrong we don't want to wait forever.
  status = zx_object_wait_one(handle_, ZX_TASK_TERMINATED,
                              zx_deadline_after(kill_timeout), &signals);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error waiting for process to die, ignoring: "
                   << util::ZxErrorString(status);
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

  if (launchpad_)
    launchpad_destroy(launchpad_);
  launchpad_ = nullptr;

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
    FXL_LOG(ERROR) << "Could not obtain a debug handle to thread: "
                   << util::ZxErrorString(status);
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
  zx_status_t status =
      zx_object_get_info(handle_, ZX_INFO_PROCESS_THREADS, nullptr, 0,
                         nullptr, &num_threads);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get process thread info (#threads): "
                   << util::ZxErrorString(status);
    return false;
  }

  auto buffer_size = num_threads * sizeof(zx_koid_t);
  auto koids = std::make_unique<zx_koid_t[]>(num_threads);
  size_t records_read;
  status = zx_object_get_info(handle_, ZX_INFO_PROCESS_THREADS,
                              koids.get(), buffer_size, &records_read, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get process thread info: "
                   << util::ZxErrorString(status);
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
                     << util::ZxErrorString(status);
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
  zx_status_t status = zx_object_get_property(process_handle,
                                              ZX_PROP_PROCESS_DEBUG_ADDR,
                                              &debug_addr, sizeof(debug_addr));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_property failed, unable to fetch dso list: "
		   << util::ZxErrorString(status);
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
  dsos_ = util::dso_fetch_list(memory_, lmap_vaddr, "app");
  // We should have fetched at least one since this is not called until the
  // dl_debug_state breakpoint is hit.
  if (dsos_ == nullptr) {
    // Don't keep trying.
    FXL_VLOG(2) << "dso_fetch_list failed";
    dsos_build_failed_ = true;
  } else {
    util::dso_vlog_list(dsos_);
    // This may already be false, but set it any for documentation purposes.
    dsos_build_failed_ = false;
  }
}

void Process::OnException(const zx_port_packet_t& packet,
                          const zx_exception_context_t& context) {
  zx_excp_type_t type = static_cast<zx_excp_type_t>(packet.type);
  Thread* thread = nullptr;
  if (packet.exception.tid != ZX_KOID_INVALID)
    thread = FindThreadById(packet.exception.tid);

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
      FXL_VLOG(1) << "Received ZX_EXCP_THREAD_STARTING exception";
      FXL_DCHECK(thread);
      FXL_DCHECK(thread->state() == Thread::State::kNew);
      thread->OnException(type, context);
      delegate_->OnThreadStarting(this, thread, context);
      break;
    case ZX_EXCP_THREAD_EXITING:
      FXL_VLOG(1) << "Received ZX_EXCP_THREAD_EXITING exception for thread "
                  << thread->GetName();
      FXL_DCHECK(thread);
      thread->OnException(type, context);
      delegate_->OnThreadExiting(this, thread, context);
      break;
    case ZX_EXCP_POLICY_ERROR:
      FXL_VLOG(1) << "Received ZX_EXCP_POLICY_ERROR exception for thread "
                  << thread->GetName();
      FXL_DCHECK(thread);
      thread->OnException(type, context);
      // TODO(dje): process synthetic exception
      break;
    default:
      FXL_LOG(ERROR) << "Ignoring unrecognized synthetic exception: " << type;
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
                   << util::ZxErrorString(status);
    return -1;
  }
}

const util::dsoinfo_t* Process::GetExecDso() {
  return dso_get_main_exec(dsos_);
}

util::dsoinfo_t* Process::LookupDso(zx_vaddr_t pc) const {
  return util::dso_lookup(dsos_, pc);
}

}  // namespace debugserver
