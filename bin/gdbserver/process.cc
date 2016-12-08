// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "process.h"

#include <cinttypes>
#include <link.h>

#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "server.h"
#include "util.h"

using std::string;
using std::vector;

// This is a global variable that exists in the dynamic linker, and thus in
// every processes's address space (since Fuchsia is PIE-only). It contains
// various information provided by the dynamic linker for use by debugging
// tools.
extern struct r_debug* _dl_debug_addr;

namespace debugserver {
namespace {

bool SetupLaunchpad(launchpad_t** out_lp, const vector<string>& argv) {
  FTL_DCHECK(out_lp);
  FTL_DCHECK(argv.size() > 0);

  // Construct the argument array.
  const char* c_args[argv.size()];
  for (size_t i = 0; i < argv.size(); ++i)
    c_args[i] = argv[i].c_str();

  launchpad_t* lp = nullptr;
  mx_status_t status = launchpad_create(0u, c_args[0], &lp);
  if (status != NO_ERROR)
    goto fail;

  status = launchpad_arguments(lp, argv.size(), c_args);
  if (status != NO_ERROR)
    goto fail;

  // TODO(armansito): Make the inferior inherit the environment (i.e.
  // launchpad_environ)?

  status = launchpad_add_vdso_vmo(lp);
  if (status != NO_ERROR)
    goto fail;

  status = launchpad_add_all_mxio(lp);
  if (status != NO_ERROR)
    goto fail;

  *out_lp = lp;
  return true;

fail:
  util::LogErrorWithMxStatus("Process setup failed", status);
  if (lp)
    launchpad_destroy(lp);
  return false;
}

bool LoadBinary(launchpad_t* lp, const string& binary_path) {
  FTL_DCHECK(lp);

  mx_status_t status =
      launchpad_elf_load(lp, launchpad_vmo_from_file(binary_path.c_str()));
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Could not load binary", status);
    return false;
  }

  status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Could not load vDSO", status);
    return false;
  }

  return true;
}

mx_koid_t GetProcessId(launchpad_t* lp) {
  FTL_DCHECK(lp);

  // We use the mx_object_get_child syscall to obtain a debug-capable handle
  // to the process. For processes, the syscall expect the ID of the underlying
  // kernel object (koid, also passing for process id in Magenta).
  mx_handle_t process_handle = launchpad_get_process_handle(lp);
  FTL_DCHECK(process_handle);

  mx_info_handle_basic_t info;
  mx_status_t status =
      mx_object_get_info(process_handle, MX_INFO_HANDLE_BASIC, &info,
                         sizeof(info), nullptr, nullptr);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("mx_object_get_info_failed", status);
    return MX_KOID_INVALID;
  }

  FTL_DCHECK(info.type == MX_OBJ_TYPE_PROCESS);

  return info.koid;
}

mx_handle_t GetProcessDebugHandle(mx_koid_t pid) {
  mx_handle_t debug_handle = MX_HANDLE_INVALID;
  mx_status_t status = mx_object_get_child(MX_HANDLE_INVALID, pid,
                                           MX_RIGHT_SAME_RIGHTS, &debug_handle);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("mx_object_get_child failed", status);
    return MX_HANDLE_INVALID;
  }

  // TODO(armansito): Check that |debug_handle| has MX_RIGHT_DEBUG (this seems
  // not to be set by anything at the moment but eventully we should check)?

  // Syscalls shouldn't return MX_HANDLE_INVALID in the case of NO_ERROR.
  FTL_DCHECK(debug_handle != MX_HANDLE_INVALID);

  return debug_handle;
}

}  // namespace

// static
const char* Process::StateName(Process::State state) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (state) {
    CASE_TO_STR(Process::State::kNew);
    CASE_TO_STR(Process::State::kStarting);
    CASE_TO_STR(Process::State::kRunning);
    CASE_TO_STR(Process::State::kGone);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

Process::Process(Server* server, Delegate* delegate, const vector<string>& argv)
    : server_(server),
      delegate_(delegate),
      argv_(argv),
      memory_(this),
      breakpoints_(this) {
  FTL_DCHECK(server_);
  FTL_DCHECK(delegate_);
}

Process::~Process() {
  if (IsAttached())
    Detach();

  if (launchpad_)
    launchpad_destroy(launchpad_);

  if (debug_handle_ != MX_HANDLE_INVALID)
    mx_handle_close(debug_handle_);

  dso_free_list(dsos_);

  // TODO(armanisto): Somehow kill the process here if it's running.
  // launchpad_destroy doesn't seem to do this.
}

std::string Process::GetName() const {
  return ftl::StringPrintf("%" PRId64, id());
}

bool Process::Initialize() {
  FTL_DCHECK(!launchpad_);
  FTL_DCHECK(!debug_handle_);
  FTL_DCHECK(!eport_key_);

  mx_status_t status;

  // The Process object survives run-after-run. Switch Gone back to New.
  switch (state_) {
    case State::kNew:
      break;
    case State::kGone:
      set_state(State::kNew);
      break;
    default:
      // Shouldn't get here if process is currently live.
      FTL_DCHECK(false);
  }

  FTL_LOG(INFO) << "Initializing process";

  if (argv_.size() == 0 || argv_[0].size() == 0) {
    FTL_LOG(ERROR) << "No program specified";
    return false;
  }

  if (!SetupLaunchpad(&launchpad_, argv_))
    return false;

  FTL_LOG(INFO) << "Process setup complete";

  if (!LoadBinary(launchpad_, argv_[0]))
    goto fail;

  FTL_VLOG(1) << "Binary loaded";

  // Initialize the PID.
  id_ = GetProcessId(launchpad_);
  FTL_DCHECK(id_ != MX_KOID_INVALID);

  // Now we need a debug-capable handle of the process.
  debug_handle_ = GetProcessDebugHandle(id_);
  if (!debug_handle_)
    goto fail;

  FTL_VLOG(1) << "Debug handle " << debug_handle_ << " obtained for process";

  status = launchpad_get_base_address(launchpad_, &base_address_);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus(
        "Failed to obtain the dynamic linker base address for process", status);
    goto fail;
  }

  status = launchpad_get_entry_address(launchpad_, &entry_address_);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus(
        "Failed to obtain the dynamic linker entry address for process",
        status);
    goto fail;
  }

  FTL_LOG(INFO) << "Obtained base load address: "
                << ftl::StringPrintf("0x%" PRIxPTR, base_address_)
                << ", entry address: "
                << ftl::StringPrintf("0x%" PRIxPTR, entry_address_);

  return true;

fail:
  id_ = MX_KOID_INVALID;
  if (debug_handle_ != MX_HANDLE_INVALID)
    mx_handle_close(debug_handle_);
  debug_handle_ = MX_HANDLE_INVALID;
  launchpad_destroy(launchpad_);
  launchpad_ = nullptr;
  return false;
}

bool Process::Attach() {
  FTL_DCHECK(debug_handle_);

  if (IsAttached()) {
    FTL_LOG(ERROR) << "Cannot attach an already attached process";
    return false;
  }

  ExceptionPort::Key key = server_->exception_port()->Bind(
      *this, std::bind(&Process::OnException, this, std::placeholders::_1,
                       std::placeholders::_2));
  if (!key) {
    FTL_LOG(ERROR) << "Failed to attach: could not bind exception port";
    return false;
  }

  eport_key_ = key;

  return true;
}

void Process::Detach() {
  // A copy of the handle is kept in ExceptionPort.BindData.
  // We can't close the process handle until we unbind the exception port,
  // so verify it's still open.
  FTL_DCHECK(debug_handle_);
  FTL_DCHECK(IsAttached());

  FTL_DCHECK(eport_key_);
  if (!server_->exception_port()->Unbind(eport_key_))
    FTL_LOG(WARNING) << "Failed to unbind exception port; ignoring";

  eport_key_ = 0;
}

bool Process::Start() {
  FTL_DCHECK(launchpad_);
  FTL_DCHECK(debug_handle_);

  if (started_) {
    FTL_LOG(ERROR) << "Process already started";
    return false;
  }

  // launchpad_start returns a dup of the process handle (owned by
  // |launchpad_|), where the original handle is given to the child. We have to
  // close the dup handle to avoid leaking it.
  mx_handle_t dup_handle = launchpad_start(launchpad_);

  // Launchpad is no longer needed after launchpad_start returns.
  launchpad_destroy(launchpad_);
  launchpad_ = nullptr;

  if (dup_handle < 0) {
    util::LogErrorWithMxStatus("Failed to start inferior process", dup_handle);
    return false;
  }
  mx_handle_close(dup_handle);

  started_ = true;
  set_state(State::kStarting);

  return true;
}

void Process::set_state(State new_state) {
  switch (new_state) {
    case State::kNew:
      FTL_DCHECK(state_ == State::kGone);
      break;
    case State::kStarting:
      FTL_DCHECK(state_ == State::kNew);
      break;
    case State::kRunning:
      FTL_DCHECK(state_ == State::kStarting);
      break;
    case State::kGone:
      FTL_DCHECK(state_ == State::kStarting || state_ == State::kRunning);
      break;
    default:
      FTL_DCHECK(false);
  }
  state_ = new_state;
}

void Process::FinishExit() {
  Detach();
  threads_.clear();

  // We close the handle here so the o/s will release the process.
  mx_handle_close(debug_handle_);
  debug_handle_ = MX_HANDLE_INVALID;
  id_ = MX_KOID_INVALID;
  base_address_ = 0;
  entry_address_ = 0;
  started_ = false;

  // Leave the state as kGone.
}

bool Process::IsAttached() const {
  return !!eport_key_;
}

Thread* Process::FindThreadById(mx_koid_t thread_id) {
  FTL_DCHECK(debug_handle_);
  if (thread_id == MX_HANDLE_INVALID) {
    FTL_LOG(ERROR) << "Invalid thread ID given: " << thread_id;
    return nullptr;
  }

  const auto iter = threads_.find(thread_id);
  if (iter != threads_.end()) {
    Thread* thread = iter->second.get();
    if (thread->state() == Thread::State::kGone) {
      FTL_VLOG(1) << "FindThreadById: Thread " << thread->GetDebugName()
                  << " is gone";
      return nullptr;
    }
    return thread;
  }

  // Try to get a debug capable handle to the child of the current process with
  // a kernel object ID that matches |thread_id|.
  mx_handle_t thread_debug_handle;
  mx_status_t status = mx_object_get_child(
      debug_handle_, thread_id, MX_RIGHT_SAME_RIGHTS, &thread_debug_handle);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Could not obtain a debug handle to thread",
                               status);
    return nullptr;
  }

  Thread* thread = new Thread(this, thread_debug_handle, thread_id);
  threads_[thread_id] = std::unique_ptr<Thread>(thread);
  return thread;
}

Thread* Process::PickOneThread() {
  if (threads_.empty())
    return nullptr;

  return threads_.begin()->second.get();
}

bool Process::RefreshAllThreads() {
  FTL_DCHECK(debug_handle_);

  // First get the thread count so that we can allocate an appropriately sized
  // buffer.
  size_t num_threads;
  mx_status_t status =
      mx_object_get_info(debug_handle_, MX_INFO_PROCESS_THREADS, nullptr, 0,
                         nullptr, &num_threads);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Failed to get process thread info", status);
    return false;
  }

  auto buffer_size = num_threads * sizeof(mx_koid_t);
  auto koids = std::make_unique<mx_koid_t[]>(num_threads);
  size_t records_read;
  status = mx_object_get_info(debug_handle_, MX_INFO_PROCESS_THREADS,
                              koids.get(), buffer_size, &records_read, nullptr);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Failed to get process thread info", status);
    return false;
  }

  FTL_DCHECK(records_read == num_threads);

  ThreadMap new_threads;
  for (size_t i = 0; i < num_threads; ++i) {
    mx_koid_t thread_id = koids[i];
    mx_handle_t thread_debug_handle = MX_HANDLE_INVALID;
    status = mx_object_get_child(debug_handle_, thread_id, MX_RIGHT_SAME_RIGHTS,
                                 &thread_debug_handle);
    if (status != NO_ERROR) {
      util::LogErrorWithMxStatus("Could not obtain a debug handle to thread",
                                 status);
      continue;
    }
    new_threads[thread_id] =
        std::make_unique<Thread>(this, thread_debug_handle, thread_id);
  }

  // Just clear the existing list and repopulate it.
  threads_ = std::move(new_threads);

  return true;
}

void Process::ForEachThread(const ThreadCallback& callback) {
  for (const auto& iter : threads_)
    callback(iter.second.get());
}

void Process::ForEachLiveThread(const ThreadCallback& callback) {
  for (const auto& iter : threads_) {
    Thread* thread = iter.second.get();
    if (thread->state() != Thread::State::kGone)
      callback(thread);
  }
}

bool Process::ReadMemory(uintptr_t address, void* out_buffer, size_t length) {
  return memory_.Read(address, out_buffer, length);
}

bool Process::WriteMemory(uintptr_t address, const void* data, size_t length) {
  return memory_.Write(address, data, length);
}

void Process::TryBuildLoadedDsosList(Thread* thread) {
  FTL_DCHECK(dsos_ == nullptr);

  if (dsos_build_failed_)
    return;

  FTL_VLOG(2) << "Building dso list";

  // TODO(dje): For now we make the simplifying assumption that the address of
  // this variable in our address space is constant among all processes.
  auto rdebug_vaddr = reinterpret_cast<mx_vaddr_t>(_dl_debug_addr);
  struct r_debug debug;
  if (!ReadMemory(rdebug_vaddr, &debug, sizeof(debug)))
    return;

  // Since we could, theoretically, stop in the dynamic linker before we get
  // that far check to see if it has been filled in.
  // TODO(dje): Document our test in dynlink.c.
  if (debug.r_version == 0) {
    FTL_VLOG(2) << "debug.r_version is 0";
    return;
  }

  // Did we stop at the dynamic linker debug breakpoint?
  bool success = thread->registers()->RefreshGeneralRegisters();
  FTL_DCHECK(success);
  mx_vaddr_t pc = thread->registers()->GetPC();
  // TODO(dje): -1: adjust_pc_after_break
  if (pc - 1 != debug.r_brk) {
    FTL_VLOG(2) << "not stopped at dynlink debug bkpt";
    return;
  }

  auto lmap_vaddr = reinterpret_cast<mx_vaddr_t>(debug.r_map);
  dsos_ = elf::dso_fetch_list(memory_, lmap_vaddr, "app");
  // We should have fetched at least one since this is not called until the
  // dl_debug_state breakpoint is hit.
  if (dsos_ == nullptr) {
    // Don't keep trying.
    FTL_VLOG(2) << "dso_fetch_list failed";
    dsos_build_failed_ = true;
  } else {
    elf::dso_vlog_list(dsos_);
  }
}

void Process::OnException(const mx_excp_type_t type,
                          const mx_exception_context_t& context) {
  Thread* thread = nullptr;
  if (context.tid != MX_KOID_INVALID)
    thread = FindThreadById(context.tid);

  // Finding the load address of the main executable requires a few steps.
  // It's not loaded until the first time we hit the _dl_debug_state
  // breakpoint. For now gdb sets that breakpoint. What we do is watch for
  // s/w breakpoint exceptions.
  if (type == MX_EXCP_SW_BREAKPOINT) {
    if (!DsosLoaded())
      TryBuildLoadedDsosList(thread);
  }

  // |type| could either map to an architectural exception or Magenta-defined
  // synthetic exceptions.
  if (MX_EXCP_IS_ARCH(type)) {
    FTL_DCHECK(thread);
    thread->OnArchException(context);
    delegate_->OnArchitecturalException(this, thread, type, context);
    return;
  }

  switch (type) {
    case MX_EXCP_START:
      FTL_VLOG(1) << "Received MX_EXCP_START exception";
      FTL_DCHECK(thread);
      FTL_DCHECK(thread->state() == Thread::State::kNew);
      delegate_->OnThreadStarted(this, thread, context);
      break;
    case MX_EXCP_GONE:
      if (thread) {
        FTL_VLOG(1) << "Received MX_EXCP_GONE exception for thread "
                    << thread->GetName();
        thread->set_state(Thread::State::kGone);
        // TODO: Split up OnProcessOrThreadExited into process/thread.
        delegate_->OnProcessOrThreadExited(this, thread, type, context);
        thread->FinishExit();
      } else {
        FTL_VLOG(1) << "Received MX_EXCP_GONE exception for process "
                    << GetName();
        set_state(Process::State::kGone);
        delegate_->OnProcessOrThreadExited(this, thread, type, context);
        FinishExit();
      }
      break;
    default:
      FTL_LOG(ERROR) << "Ignoring unrecognized synthetic exception: " << type;
      break;
  }
}

int Process::ExitCode() {
  FTL_DCHECK(state_ == State::kGone);
  mx_info_process_t info;
  auto status = mx_object_get_info(handle(), MX_INFO_PROCESS, &info,
                                   sizeof(info), NULL, NULL);
  if (status == NO_ERROR) {
    FTL_LOG(INFO) << "Process exited with code " << info.return_code;
    return info.return_code;
  } else {
    util::LogErrorWithMxStatus("Error getting process exit code", status);
    return -1;
  }
}

const elf::dsoinfo_t* Process::GetExecDso() {
  return dso_get_main_exec(dsos_);
}

}  // namespace debugserver
