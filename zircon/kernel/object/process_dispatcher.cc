// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/process_dispatcher.h"

#include <assert.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/crypto/global_prng.h>
#include <lib/ktrace.h>
#include <string.h>
#include <trace.h>
#include <zircon/listnode.h>
#include <zircon/rights.h>

#include <arch/defines.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <kernel/thread.h>
#include <object/diagnostics.h>
#include <object/futex_context.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_process_create_count, "dispatcher.process.create")
KCOUNTER(dispatcher_process_destroy_count, "dispatcher.process.destroy")

constexpr uint32_t kHandleMustBeOneMask = ((0x1u << kHandleReservedBits) - 1);
static_assert(kHandleMustBeOneMask == ZX_HANDLE_FIXED_BITS_MASK,
              "kHandleMustBeOneMask must match ZX_HANDLE_FIXED_BITS_MASK!");

static zx_handle_t map_handle_to_value(const Handle* handle, uint32_t mixer) {
  // Ensure that the last two bits of the result is not zero, and make sure we
  // don't lose any base_value bits when shifting.
  constexpr uint32_t kBaseValueMustBeZeroMask =
      (kHandleMustBeOneMask << ((sizeof(handle->base_value()) * 8) - kHandleReservedBits));

  DEBUG_ASSERT((mixer & kHandleMustBeOneMask) == 0);
  DEBUG_ASSERT((handle->base_value() & kBaseValueMustBeZeroMask) == 0);

  auto handle_id = (handle->base_value() << kHandleReservedBits) | kHandleMustBeOneMask;

  return static_cast<zx_handle_t>(mixer ^ handle_id);
}

static Handle* map_value_to_handle(zx_handle_t value, uint32_t mixer) {
  // Validate that the "must be one" bits are actually one.
  if ((value & kHandleMustBeOneMask) != kHandleMustBeOneMask) {
    return nullptr;
  }

  uint32_t handle_id = (static_cast<uint32_t>(value) ^ mixer) >> kHandleReservedBits;
  return Handle::FromU32(handle_id);
}

zx_status_t ProcessDispatcher::Create(fbl::RefPtr<JobDispatcher> job, ktl::string_view name,
                                      uint32_t flags, KernelHandle<ProcessDispatcher>* handle,
                                      zx_rights_t* rights,
                                      KernelHandle<VmAddressRegionDispatcher>* root_vmar_handle,
                                      zx_rights_t* root_vmar_rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) ProcessDispatcher(job, name, flags)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  zx_status_t result = new_handle.dispatcher()->Initialize();
  if (result != ZX_OK)
    return result;

  // Create a dispatcher for the root VMAR.
  KernelHandle<VmAddressRegionDispatcher> new_vmar_handle;
  result = VmAddressRegionDispatcher::Create(new_handle.dispatcher()->aspace()->RootVmar(),
                                             ARCH_MMU_FLAG_PERM_USER, &new_vmar_handle,
                                             root_vmar_rights);
  if (result != ZX_OK)
    return result;

  // Only now that the process has been fully created and initialized can we register it with its
  // parent job. We don't want anyone to see it in a partially initalized state.
  if (!job->AddChildProcess(new_handle.dispatcher())) {
    return ZX_ERR_BAD_STATE;
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  *root_vmar_handle = ktl::move(new_vmar_handle);

  return ZX_OK;
}

ProcessDispatcher::ProcessDispatcher(fbl::RefPtr<JobDispatcher> job, ktl::string_view name,
                                     uint32_t flags)
    : job_(ktl::move(job)),
      policy_(job_->GetPolicy()),
      exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_PROCESS),
      debug_exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER),
      name_(name.data(), name.length()) {
  LTRACE_ENTRY_OBJ;

  kcounter_add(dispatcher_process_create_count, 1);

  // Generate handle XOR mask with top bit and bottom two bits cleared
  uint32_t secret;
  auto prng = crypto::GlobalPRNG::GetInstance();
  prng->Draw(&secret, sizeof(secret));

  // Handle values must always have the low kHandleReservedBits set.  Do not
  // ever attempt to toggle these bits using the handle_rand_ xor mask.
  handle_rand_ = secret << kHandleReservedBits;
}

ProcessDispatcher::~ProcessDispatcher() {
  LTRACE_ENTRY_OBJ;

  DEBUG_ASSERT(state_ == State::INITIAL || state_ == State::DEAD);

  // Assert that the -> DEAD transition cleaned up what it should have.
  DEBUG_ASSERT(handle_table_.is_empty());
  DEBUG_ASSERT(!aspace_ || aspace_->is_destroyed());
  DEBUG_ASSERT(handle_table_count_ == 0);

  kcounter_add(dispatcher_process_destroy_count, 1);

  // Remove ourselves from the parent job's raw ref to us. Note that this might
  // have been called when transitioning State::DEAD. The Job can handle double calls.
  job_->RemoveChildProcess(this);

  LTRACE_EXIT_OBJ;
}

void ProcessDispatcher::on_zero_handles() {
  // If the process is in the initial state and the last handle is closed
  // we never detach from the parent job, so run the shutdown sequence for
  // that case.
  {
    Guard<Mutex> guard{get_lock()};
    if (state_ != State::INITIAL) {
      // Use the normal cleanup path instead.
      return;
    }
    SetStateLocked(State::DEAD);
  }

  FinishDeadTransition();
}

void ProcessDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  name_.get(ZX_MAX_NAME_LEN, out_name);
}

zx_status_t ProcessDispatcher::set_name(const char* name, size_t len) {
  return name_.set(name, len);
}

zx_status_t ProcessDispatcher::Initialize() {
  LTRACE_ENTRY_OBJ;

  Guard<Mutex> guard{get_lock()};

  DEBUG_ASSERT(state_ == State::INITIAL);

  // create an address space for this process, named after the process's koid.
  char aspace_name[ZX_MAX_NAME_LEN];
  snprintf(aspace_name, sizeof(aspace_name), "proc:%" PRIu64, get_koid());
  aspace_ = VmAspace::Create(VmAspace::TYPE_USER, aspace_name);
  if (!aspace_) {
    TRACEF("error creating address space\n");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

void ProcessDispatcher::Exit(int64_t retcode) {
  LTRACE_ENTRY_OBJ;

  DEBUG_ASSERT(ProcessDispatcher::GetCurrent() == this);

  {
    Guard<Mutex> guard{get_lock()};

    // check that we're in the RUNNING state or we're racing with something
    // else that has already pushed us until the DYING state
    DEBUG_ASSERT_MSG(state_ == State::RUNNING || state_ == State::DYING, "state is %s",
                     StateToString(state_));

    // Set the exit status if there isn't already an exit in progress.
    if (state_ != State::DYING) {
      DEBUG_ASSERT(retcode_ == 0);
      retcode_ = retcode;
    }

    // enter the dying state, which should kill all threads
    SetStateLocked(State::DYING);
  }

  ThreadDispatcher::ExitCurrent();

  __UNREACHABLE;
}

void ProcessDispatcher::Kill(int64_t retcode) {
  LTRACE_ENTRY_OBJ;

  // ZX-880: Call RemoveChildProcess outside of |get_lock()|.
  bool became_dead = false;

  {
    Guard<Mutex> guard{get_lock()};

    // we're already dead
    if (state_ == State::DEAD)
      return;

    if (state_ != State::DYING) {
      DEBUG_ASSERT(retcode_ == 0);
      retcode_ = retcode;
    }

    // if we have no threads, enter the dead state directly
    if (thread_list_.is_empty()) {
      SetStateLocked(State::DEAD);
      became_dead = true;
    } else {
      // enter the dying state, which should trigger a thread kill.
      // the last thread exiting will transition us to DEAD
      SetStateLocked(State::DYING);
    }
  }

  if (became_dead)
    FinishDeadTransition();
}

zx_status_t ProcessDispatcher::Suspend() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<Mutex> guard{get_lock()};

  // If we're dying don't try to suspend.
  if (state_ == State::DYING || state_ == State::DEAD)
    return ZX_ERR_BAD_STATE;

  DEBUG_ASSERT(suspend_count_ >= 0);
  suspend_count_++;
  if (suspend_count_ == 1) {
    for (auto& thread : thread_list_) {
      // Thread suspend can only fail if the thread is already dying, which is fine here
      // since it will be removed from this process shortly, so continue to suspend whether
      // the thread suspend succeeds or fails.
      zx_status_t status = thread.Suspend();
      DEBUG_ASSERT(status == ZX_OK || thread.IsDyingOrDead());
    }
  }

  return ZX_OK;
}

void ProcessDispatcher::Resume() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<Mutex> guard{get_lock()};

  // If we're in the process of dying don't try to resume, just let it continue to clean up.
  if (state_ == State::DYING || state_ == State::DEAD)
    return;

  DEBUG_ASSERT(suspend_count_ > 0);
  suspend_count_--;
  if (suspend_count_ == 0) {
    for (auto& thread : thread_list_) {
      thread.Resume();
    }
  }
}

void ProcessDispatcher::KillAllThreadsLocked() {
  LTRACE_ENTRY_OBJ;

  for (auto& thread : thread_list_) {
    LTRACEF("killing thread %p\n", &thread);
    thread.Kill();
  }
}

zx_status_t ProcessDispatcher::AddInitializedThread(ThreadDispatcher* t, bool initial_thread,
                                                    const ThreadDispatcher::EntryState& entry) {
  LTRACE_ENTRY_OBJ;

  Guard<Mutex> guard{get_lock()};

  if (initial_thread) {
    if (state_ != State::INITIAL)
      return ZX_ERR_BAD_STATE;
  } else {
    // We must not add a thread when in the DYING or DEAD states.
    // Also, we want to ensure that this is not the first thread.
    if (state_ != State::RUNNING)
      return ZX_ERR_BAD_STATE;
  }

  // Now that we know our state is okay we can attempt to start the thread running. This is okay
  // since as long as the thread doesn't refuse to start running then we cannot fail from here
  // and so we will update our thread_list_ and state before we drop the lock, making this
  // whole process atomic to any observers.
  zx_status_t result = t->MakeRunnable(entry, suspend_count_ > 0);
  if (result != ZX_OK) {
    return result;
  }

  // add the thread to our list
  DEBUG_ASSERT(thread_list_.is_empty() == initial_thread);
  thread_list_.push_back(t);

  DEBUG_ASSERT(t->process() == this);

  if (initial_thread)
    SetStateLocked(State::RUNNING);

  return ZX_OK;
}

// This is called within thread T's context when it is exiting.

void ProcessDispatcher::RemoveThread(ThreadDispatcher* t) {
  LTRACE_ENTRY_OBJ;

  // ZX-880: Call RemoveChildProcess outside of |get_lock()|.
  bool became_dead = false;

  {
    // we're going to check for state and possibly transition below
    Guard<Mutex> guard{get_lock()};

    // remove the thread from our list
    DEBUG_ASSERT(t != nullptr);
    thread_list_.erase(*t);

    // if this was the last thread, transition directly to DEAD state
    if (thread_list_.is_empty()) {
      LTRACEF("last thread left the process %p, entering DEAD state\n", this);
      SetStateLocked(State::DEAD);
      became_dead = true;
    }

    Thread::RuntimeStats child_runtime;
    if (t->GetRuntimeStats(&child_runtime) == ZX_OK) {
      aggregated_runtime_stats_.Add(child_runtime.TotalRuntime());
    }
  }

  if (became_dead)
    FinishDeadTransition();
}

zx_koid_t ProcessDispatcher::get_related_koid() const { return job_->get_koid(); }

ProcessDispatcher::State ProcessDispatcher::state() const {
  Guard<Mutex> guard{get_lock()};
  return state_;
}

fbl::RefPtr<JobDispatcher> ProcessDispatcher::job() { return job_; }

void ProcessDispatcher::SetStateLocked(State s) {
  LTRACEF("process %p: state %u (%s)\n", this, static_cast<unsigned int>(s), StateToString(s));

  DEBUG_ASSERT(get_lock()->lock().IsHeld());

  // look for some invalid state transitions
  if (state_ == State::DEAD && s != State::DEAD) {
    panic("ProcessDispatcher::SetStateLocked invalid state transition from DEAD to !DEAD\n");
    return;
  }

  // transitions to your own state are okay
  if (s == state_)
    return;

  state_ = s;

  if (s == State::DYING) {
    // send kill to all of our threads
    KillAllThreadsLocked();
  }
}

// Finish processing of the transition to State::DEAD. Some things need to be done
// outside of holding |get_lock()|. Beware this is called from several places
// including on_zero_handles().
void ProcessDispatcher::FinishDeadTransition() {
  DEBUG_ASSERT(!completely_dead_);
  completely_dead_ = true;

  // It doesn't matter whether the lock is held or not while shutting down
  // the exceptionates, this is just the most convenient place to do it.
  exceptionate_.Shutdown();
  debug_exceptionate_.Shutdown();

  // clean up the handle table
  LTRACEF_LEVEL(2, "cleaning up handle table on proc %p\n", this);

  HandleList to_clean;
  {
    Guard<BrwLockPi, BrwLockPi::Writer> guard{&handle_table_lock_};
    for (auto& cursor : handle_table_cursors_) {
      cursor.Invalidate();
    }
    for (auto& handle : handle_table_) {
      handle.set_process_id(ZX_KOID_INVALID);
    }
    handle_table_count_ = 0;
    to_clean.swap(handle_table_);
  }

  // This needs to be done outside of |get_lock()|.
  while (!to_clean.is_empty()) {
    // Delete handle via HandleOwner dtor.
    HandleOwner ho(to_clean.pop_front());
  }

  LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

  // Tear down the address space. It may not exist if Initialize() failed.
  if (aspace_)
    aspace_->Destroy();

  // signal waiter
  LTRACEF_LEVEL(2, "signaling waiters\n");
  UpdateState(0u, ZX_TASK_TERMINATED);

  // The PROC_CREATE record currently emits a uint32_t koid.
  uint32_t koid = static_cast<uint32_t>(get_koid());
  ktrace(TAG_PROC_EXIT, koid, 0, 0, 0);

  // Call job_->RemoveChildProcess(this) outside of |get_lock()|. Otherwise
  // we risk a deadlock as we have |get_lock()| and RemoveChildProcess grabs
  // the job's |lock_|, whereas JobDispatcher::EnumerateChildren obtains the
  // locks in the opposite order. We want to keep lock acquisition order
  // consistent, and JobDispatcher::EnumerateChildren's order makes
  // sense. We don't need |get_lock()| when calling RemoveChildProcess
  // here. ZX-880
  job_->RemoveChildProcess(this);

  // If we are critical to a job, we need to take action. Similar to the above
  // comment, we avoid performing the actual call into the job whilst still
  // holding the lock.
  fbl::RefPtr<JobDispatcher> kill_job;
  {
    Guard<Mutex> guard{get_lock()};
    if (critical_to_job_ != nullptr) {
      // Check if we accept any return code, or require it be non-zero.
      if (!retcode_nonzero_ || retcode_ != 0) {
        kill_job = critical_to_job_;
      }
    }
  }
  if (kill_job) {
    kill_job->Kill(ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
  }
}

// process handle manipulation routines
zx_handle_t ProcessDispatcher::MapHandleToValue(const Handle* handle) const {
  return map_handle_to_value(handle, handle_rand_);
}

zx_handle_t ProcessDispatcher::MapHandleToValue(const HandleOwner& handle) const {
  return map_handle_to_value(handle.get(), handle_rand_);
}

Handle* ProcessDispatcher::GetHandleLocked(zx_handle_t handle_value, bool skip_policy) {
  auto handle = map_value_to_handle(handle_value, handle_rand_);
  if (handle && handle->process_id() == get_koid())
    return handle;

  if (likely(!skip_policy)) {
    // Handle lookup failed.  We potentially generate an exception or kill the process,
    // depending on the job policy. Note that we don't use the return value from
    // EnforceBasicPolicy() here: ZX_POL_ACTION_ALLOW and ZX_POL_ACTION_DENY are equivalent for
    // ZX_POL_BAD_HANDLE.
    __UNUSED auto result = EnforceBasicPolicy(ZX_POL_BAD_HANDLE);
  }

  return nullptr;
}

uint32_t ProcessDispatcher::HandleCount() const {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
  return handle_table_count_;
}

void ProcessDispatcher::AddHandle(HandleOwner handle) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&handle_table_lock_};
  AddHandleLocked(ktl::move(handle));
}

void ProcessDispatcher::AddHandleLocked(HandleOwner handle) {
  handle->set_process_id(get_koid());
  handle_table_.push_front(handle.release());
  handle_table_count_++;
}

HandleOwner ProcessDispatcher::RemoveHandleLocked(Handle* handle) {
  DEBUG_ASSERT(handle_table_count_ > 0);
  handle->set_process_id(ZX_KOID_INVALID);
  // Make sure we don't leave any dangling cursors.
  for (auto& cursor : handle_table_cursors_) {
    // If it points to |handle|, skip over it.
    cursor.AdvanceIf(handle);
  }
  handle_table_.erase(*handle);
  handle_table_count_--;
  return HandleOwner(handle);
}

HandleOwner ProcessDispatcher::RemoveHandle(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&handle_table_lock_};
  return RemoveHandleLocked(handle_value);
}

HandleOwner ProcessDispatcher::RemoveHandleLocked(zx_handle_t handle_value) {
  auto handle = GetHandleLocked(handle_value);
  if (!handle)
    return nullptr;
  return RemoveHandleLocked(handle);
}

zx_status_t ProcessDispatcher::RemoveHandles(ktl::span<const zx_handle_t> handles) {
  zx_status_t status = ZX_OK;
  Guard<BrwLockPi, BrwLockPi::Writer> guard{handle_table_lock()};

  for (zx_handle_t handle_value : handles) {
    if (handle_value == ZX_HANDLE_INVALID)
      continue;
    auto handle = RemoveHandleLocked(handle_value);
    if (!handle)
      status = ZX_ERR_BAD_HANDLE;
  }
  return status;
}

zx_koid_t ProcessDispatcher::GetKoidForHandle(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
  Handle* handle = GetHandleLocked(handle_value);
  if (!handle)
    return ZX_KOID_INVALID;
  return handle->dispatcher()->get_koid();
}

zx_status_t ProcessDispatcher::GetDispatcherInternal(zx_handle_t handle_value,
                                                     fbl::RefPtr<Dispatcher>* dispatcher,
                                                     zx_rights_t* rights) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
  Handle* handle = GetHandleLocked(handle_value);
  if (!handle)
    return ZX_ERR_BAD_HANDLE;

  *dispatcher = handle->dispatcher();
  if (rights)
    *rights = handle->rights();
  return ZX_OK;
}

void ProcessDispatcher::GetInfo(zx_info_process_t* info) const {
  canary_.Assert();

  State state;
  // retcode_ depends on the state: make sure they're consistent.
  {
    Guard<Mutex> guard{get_lock()};
    state = state_;
    info->return_code = retcode_;
    // TODO: Protect with rights if necessary.
    info->debugger_attached = debug_exceptionate_.HasValidChannel();
  }

  switch (state) {
    case State::DEAD:
    case State::DYING:
      info->exited = true;
      __FALLTHROUGH;
    case State::RUNNING:
      info->started = true;
      break;
    case State::INITIAL:
    default:
      break;
  }
}

zx_status_t ProcessDispatcher::GetStats(zx_info_task_stats_t* stats) const {
  DEBUG_ASSERT(stats != nullptr);
  Guard<Mutex> guard{get_lock()};
  if (state_ == State::DEAD) {
    return ZX_ERR_BAD_STATE;
  }
  VmAspace::vm_usage_t usage;
  zx_status_t s = aspace_->GetMemoryUsage(&usage);
  if (s != ZX_OK) {
    return s;
  }
  stats->mem_mapped_bytes = usage.mapped_pages * PAGE_SIZE;
  stats->mem_private_bytes = usage.private_pages * PAGE_SIZE;
  stats->mem_shared_bytes = usage.shared_pages * PAGE_SIZE;
  stats->mem_scaled_shared_bytes = usage.scaled_shared_bytes;
  return ZX_OK;
}

zx_status_t ProcessDispatcher::AccumulateRuntimeTo(zx_info_task_runtime_t* info) const {
  Guard<Mutex> guard{get_lock()};
  aggregated_runtime_stats_.AccumulateRuntimeTo(info);
  for (const auto& thread : thread_list_) {
    zx_status_t err = thread.AccumulateRuntimeTo(info);
    if (err != ZX_OK) {
      return err;
    }
  }

  return ZX_OK;
}

zx_status_t ProcessDispatcher::GetAspaceMaps(VmAspace* current_aspace,
                                             user_out_ptr<zx_info_maps_t> maps, size_t max,
                                             size_t* actual, size_t* available) const {
  Guard<Mutex> guard{get_lock()};
  if (state_ == State::DEAD) {
    return ZX_ERR_BAD_STATE;
  }
  return GetVmAspaceMaps(current_aspace, aspace_, maps, max, actual, available);
}

zx_status_t ProcessDispatcher::GetVmos(VmAspace* current_aspace, VmoInfoWriter& vmos, size_t max,
                                       size_t* actual_out, size_t* available_out) {
  {
    Guard<Mutex> guard{get_lock()};
    if (state_ != State::RUNNING) {
      return ZX_ERR_BAD_STATE;
    }
  }

  size_t actual = 0;
  size_t available = 0;
  zx_status_t s = GetProcessVmos(this, vmos, max, &actual, &available);
  if (s != ZX_OK) {
    return s;
  }

  size_t actual2 = 0;
  size_t available2 = 0;
  DEBUG_ASSERT(max >= actual);
  vmos.AddOffset(actual);
  s = GetVmAspaceVmos(current_aspace, aspace_, vmos, max - actual, &actual2, &available2);
  if (s != ZX_OK) {
    return s;
  }
  *actual_out = actual + actual2;
  *available_out = available + available2;
  return ZX_OK;
}

zx_status_t ProcessDispatcher::GetThreads(fbl::Array<zx_koid_t>* out_threads) const {
  Guard<Mutex> guard{get_lock()};
  size_t n = thread_list_.size_slow();
  fbl::Array<zx_koid_t> threads;
  fbl::AllocChecker ac;
  threads.reset(new (&ac) zx_koid_t[n], n);
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;
  size_t i = 0;
  for (auto& thread : thread_list_) {
    threads[i] = thread.get_koid();
    ++i;
  }
  DEBUG_ASSERT(i == n);
  *out_threads = ktl::move(threads);
  return ZX_OK;
}

zx_status_t ProcessDispatcher::SetCriticalToJob(fbl::RefPtr<JobDispatcher> critical_to_job,
                                                bool retcode_nonzero) {
  Guard<Mutex> guard{get_lock()};

  if (critical_to_job_) {
    // The process is already critical to a job.
    return ZX_ERR_ALREADY_BOUND;
  }

  auto job_copy = job_;
  for (auto& job = job_copy; job; job = job->parent()) {
    if (job == critical_to_job) {
      critical_to_job_ = critical_to_job;
      break;
    }
  }

  if (!critical_to_job_) {
    // The provided job is not the parent of this process, or an ancestor.
    return ZX_ERR_INVALID_ARGS;
  }

  retcode_nonzero_ = retcode_nonzero;
  return ZX_OK;
}

zx_status_t ProcessDispatcher::GetHandleInfo(fbl::Array<zx_info_handle_extended_t>* handles) const {
  for (;;) {
    size_t count = HandleCount();
    // TODO: Bug 45685. This memory allocation should come from a different pool since it
    // can be larger than one page.
    fbl::AllocChecker ac;
    handles->reset(new (&ac) zx_info_handle_extended_t[count], count);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    {
      Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
      if (count != handle_table_count_) {
        continue;
      }

      size_t index = 0;
      ForEachHandleLocked([&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        auto& entry = (*handles)[index++];
        entry = {disp->get_type(),
                 handle,
                 rights,
                 0u,
                 disp->get_koid(),
                 disp->get_related_koid(),
                 0u};
        return ZX_OK;
      });
    }
    return ZX_OK;
  }
}

Exceptionate* ProcessDispatcher::exceptionate(Exceptionate::Type type) {
  canary_.Assert();
  return type == Exceptionate::Type::kDebug ? &debug_exceptionate_ : &exceptionate_;
}

uint32_t ProcessDispatcher::ThreadCount() const {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};
  return static_cast<uint32_t>(thread_list_.size_slow());
}

size_t ProcessDispatcher::PageCount() const {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};
  if (state_ != State::RUNNING) {
    return 0;
  }
  return aspace_->AllocatedPages();
}

class FindProcessByKoid final : public JobEnumerator {
 public:
  FindProcessByKoid(zx_koid_t koid) : koid_(koid) {}
  FindProcessByKoid(const FindProcessByKoid&) = delete;

  // To be called after enumeration.
  fbl::RefPtr<ProcessDispatcher> get_pd() { return pd_; }

 private:
  bool OnProcess(ProcessDispatcher* process) final {
    if (process->get_koid() == koid_) {
      pd_ = fbl::RefPtr(process);
      // Stop the enumeration.
      return false;
    }
    // Keep looking.
    return true;
  }

  const zx_koid_t koid_;
  fbl::RefPtr<ProcessDispatcher> pd_ = nullptr;
};

// static
fbl::RefPtr<ProcessDispatcher> ProcessDispatcher::LookupProcessById(zx_koid_t koid) {
  FindProcessByKoid finder(koid);
  GetRootJobDispatcher()->EnumerateChildren(&finder, /* recurse */ true);
  return finder.get_pd();
}

fbl::RefPtr<ThreadDispatcher> ProcessDispatcher::LookupThreadById(zx_koid_t koid) {
  LTRACE_ENTRY_OBJ;
  Guard<Mutex> guard{get_lock()};

  auto iter =
      thread_list_.find_if([koid](const ThreadDispatcher& t) { return t.get_koid() == koid; });
  return fbl::RefPtr(iter.CopyPointer());
}

uintptr_t ProcessDispatcher::get_debug_addr() const {
  Guard<Mutex> guard{get_lock()};
  return debug_addr_;
}

zx_status_t ProcessDispatcher::set_debug_addr(uintptr_t addr) {
  if (addr == 0u)
    return ZX_ERR_INVALID_ARGS;
  Guard<Mutex> guard{get_lock()};
  // Only allow the value to be set to a nonzero or magic debug break once:
  // Once ld.so has set it that's it.
  if (!(debug_addr_ == 0u || debug_addr_ == ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET))
    return ZX_ERR_ACCESS_DENIED;
  debug_addr_ = addr;
  return ZX_OK;
}

uintptr_t ProcessDispatcher::get_dyn_break_on_load() const {
  Guard<Mutex> guard{get_lock()};
  return dyn_break_on_load_;
}

zx_status_t ProcessDispatcher::set_dyn_break_on_load(uintptr_t break_on_load) {
  Guard<Mutex> guard{get_lock()};
  dyn_break_on_load_ = break_on_load;
  return ZX_OK;
}

zx_status_t ProcessDispatcher::EnforceBasicPolicy(uint32_t condition) {
  const auto action = policy_.QueryBasicPolicy(condition);
  switch (action) {
    case ZX_POL_ACTION_ALLOW:
      // Not calling IncrementCounter here because this is the common case (fast path).
      return ZX_OK;
    case ZX_POL_ACTION_DENY:
      JobPolicy::IncrementCounter(action, condition);
      return ZX_ERR_ACCESS_DENIED;
    case ZX_POL_ACTION_ALLOW_EXCEPTION:
      Thread::Current::SignalPolicyException();
      JobPolicy::IncrementCounter(action, condition);
      return ZX_OK;
    case ZX_POL_ACTION_DENY_EXCEPTION:
      Thread::Current::SignalPolicyException();
      JobPolicy::IncrementCounter(action, condition);
      return ZX_ERR_ACCESS_DENIED;
    case ZX_POL_ACTION_KILL:
      Kill(ZX_TASK_RETCODE_POLICY_KILL);
      JobPolicy::IncrementCounter(action, condition);
      // Because we've killed, this return value will never make it out to usermode. However,
      // callers of this method will see and act on it.
      return ZX_ERR_ACCESS_DENIED;
  };
  panic("unexpected policy action %u\n", action);
}

TimerSlack ProcessDispatcher::GetTimerSlackPolicy() const { return policy_.GetTimerSlack(); }

TaskRuntimeStats ProcessDispatcher::GetAggregatedRuntime() const {
  Guard<Mutex> guard{get_lock()};
  return aggregated_runtime_stats_;
}

uintptr_t ProcessDispatcher::cache_vdso_code_address() {
  Guard<Mutex> guard{get_lock()};
  vdso_code_address_ = aspace_->vdso_code_address();
  return vdso_code_address_;
}

const char* StateToString(ProcessDispatcher::State state) {
  switch (state) {
    case ProcessDispatcher::State::INITIAL:
      return "initial";
    case ProcessDispatcher::State::RUNNING:
      return "running";
    case ProcessDispatcher::State::DYING:
      return "dying";
    case ProcessDispatcher::State::DEAD:
      return "dead";
  }
  return "unknown";
}

bool ProcessDispatcher::IsHandleValid(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
  return (GetHandleLocked(handle_value) != nullptr);
}

void ProcessDispatcher::OnProcessStartForJobDebugger(ThreadDispatcher* t,
                                                     const arch_exception_context_t* context) {
  auto job = job_;
  while (job) {
    if (t->HandleSingleShotException(job->exceptionate(Exceptionate::Type::kDebug),
                                     ZX_EXCP_PROCESS_STARTING, *context)) {
      break;
    }

    job = job->parent();
  }
}

ProcessDispatcher::HandleCursor::HandleCursor(ProcessDispatcher* process) : process_(process) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&process_->handle_table_lock_};
  if (!process_->handle_table_.is_empty()) {
    iter_ = process_->handle_table_.begin();
  } else {
    iter_ = process_->handle_table_.end();
  }

  // Register so this cursor can be invalidated or advanced if the handle it points to is removed.
  process_->handle_table_cursors_.push_front(this);
}

ProcessDispatcher::HandleCursor::~HandleCursor() {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&process_->handle_table_lock_};
  process_->handle_table_cursors_.erase(*this);
}

void ProcessDispatcher::HandleCursor::Invalidate() { iter_ = process_->handle_table_.end(); }

Handle* ProcessDispatcher::HandleCursor::Next() {
  if (iter_ == process_->handle_table_.end()) {
    return nullptr;
  }

  Handle* result = &*iter_;
  iter_++;
  return result;
}

void ProcessDispatcher::HandleCursor::AdvanceIf(const Handle* h) {
  if (iter_ != process_->handle_table_.end()) {
    if (&*iter_ == h) {
      iter_++;
    }
  }
}
