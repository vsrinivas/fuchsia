// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PROCESS_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PROCESS_DISPATCHER_H_

#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/brwlock.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/task_runtime_stats.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/forward.h>
#include <ktl/span.h>
#include <object/dispatcher.h>
#include <object/exceptionate.h>
#include <object/futex_context.h>
#include <object/handle.h>
#include <object/job_policy.h>
#include <object/thread_dispatcher.h>
#include <vm/vm_aspace.h>

class JobDispatcher;
class VmoInfoWriter;

namespace internal {
// Tag for a ProcessDispatcher's parent JobDispatcher's raw job list.
struct ProcessDispatcherRawJobListTag {};
// Tag for a ProcessDispatcher's parent JobDispatcher's job list.
struct ProcessDispatcherJobListTag {};
}  // namespace internal

class ProcessDispatcher final
    : public SoloDispatcher<ProcessDispatcher, ZX_DEFAULT_PROCESS_RIGHTS>,
      public fbl::ContainableBaseClasses<
          fbl::TaggedDoublyLinkedListable<ProcessDispatcher*,
                                          internal::ProcessDispatcherRawJobListTag>,
          fbl::TaggedSinglyLinkedListable<fbl::RefPtr<ProcessDispatcher>,
                                          internal::ProcessDispatcherJobListTag>> {
 public:
  using RawJobListTag = internal::ProcessDispatcherRawJobListTag;
  using JobListTag = internal::ProcessDispatcherJobListTag;

  static zx_status_t Create(fbl::RefPtr<JobDispatcher> job, ktl::string_view name, uint32_t flags,
                            KernelHandle<ProcessDispatcher>* handle, zx_rights_t* rights,
                            KernelHandle<VmAddressRegionDispatcher>* root_vmar_handle,
                            zx_rights_t* root_vmar_rights);

  static ProcessDispatcher* GetCurrent() {
    ThreadDispatcher* current = ThreadDispatcher::GetCurrent();
    DEBUG_ASSERT(current);
    return current->process();
  }

  static void ExitCurrent(int64_t retcode) __NO_RETURN {
    ThreadDispatcher* current = ThreadDispatcher::GetCurrent();
    DEBUG_ASSERT(current);
    current->process()->Exit(retcode);
  }

  // Dispatcher implementation
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PROCESS; }
  void on_zero_handles() final;
  zx_koid_t get_related_koid() const final;

  ~ProcessDispatcher() final;

  // state of the process
  enum class State {
    INITIAL,  // initial state, no thread present in process
    RUNNING,  // first thread has started and is running
    DYING,    // process has delivered kill signal to all threads
    DEAD,     // all threads have entered DEAD state and potentially dropped refs on process
  };

  // Performs initialization on a newly constructed ProcessDispatcher
  // If this fails, then the object is invalid and should be deleted
  zx_status_t Initialize();

  // Maps a |handle| to an integer which can be given to usermode as a
  // handle value. Uses Handle->base_value() plus additional mixing.
  zx_handle_t MapHandleToValue(const Handle* handle) const;
  zx_handle_t MapHandleToValue(const HandleOwner& handle) const;

  // Maps a handle value into a Handle as long we can verify that
  // it belongs to this process. Use |skip_policy = true| for testing that
  // a handle is valid without potentially triggering a job policy exception.
  Handle* GetHandleLocked(zx_handle_t handle_value, bool skip_policy = false)
      TA_REQ_SHARED(handle_table_lock_);

  // Returns the number of outstanding handles in this process handle table.
  uint32_t HandleCount() const;

  // Adds |handle| to this process handle list. The handle->process_id() is
  // set to this process id().
  void AddHandle(HandleOwner handle);
  void AddHandleLocked(HandleOwner handle) TA_REQ(handle_table_lock_);

  // Set of overloads that remove the |handle| or |handle_value| from this process
  // handle list and returns ownership to the handle.
  HandleOwner RemoveHandleLocked(Handle* handle) TA_REQ(handle_table_lock_);
  HandleOwner RemoveHandleLocked(zx_handle_t handle_value) TA_REQ(handle_table_lock_);
  HandleOwner RemoveHandle(zx_handle_t handle_value);

  // Remove all of an array of |handles| from the process. Returns ZX_OK if all of the
  // handles were removed, and returns ZX_ERR_BAD_HANDLE if any were not.
  zx_status_t RemoveHandles(ktl::span<const zx_handle_t> handles);

  // Get the dispatcher corresponding to this handle value.
  template <typename T>
  zx_status_t GetDispatcher(zx_handle_t handle_value, fbl::RefPtr<T>* dispatcher) {
    return GetDispatcherAndRights(handle_value, dispatcher, nullptr);
  }

  // Get the dispatcher and the rights corresponding to this handle value.
  template <typename T>
  zx_status_t GetDispatcherAndRights(zx_handle_t handle_value, fbl::RefPtr<T>* dispatcher,
                                     zx_rights_t* out_rights) {
    fbl::RefPtr<Dispatcher> generic_dispatcher;
    auto status = GetDispatcherInternal(handle_value, &generic_dispatcher, out_rights);
    if (status != ZX_OK)
      return status;
    *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
    if (!*dispatcher)
      return ZX_ERR_WRONG_TYPE;
    return ZX_OK;
  }

  template <typename T>
  zx_status_t GetDispatcherWithRightsNoPolicyCheck(zx_handle_t handle_value,
                                                   zx_rights_t desired_rights,
                                                   fbl::RefPtr<T>* dispatcher,
                                                   zx_rights_t* out_rights) {
    return GetDispatcherWithRightsImpl(handle_value, desired_rights, dispatcher, out_rights, true);
  }

  template <typename T>
  zx_status_t GetDispatcherWithRights(zx_handle_t handle_value, zx_rights_t desired_rights,
                                      fbl::RefPtr<T>* dispatcher, zx_rights_t* out_rights) {
    return GetDispatcherWithRightsImpl(handle_value, desired_rights, dispatcher, out_rights, false);
  }

  // Get the dispatcher corresponding to this handle value, after
  // checking that this handle has the desired rights.
  template <typename T>
  zx_status_t GetDispatcherWithRights(zx_handle_t handle_value, zx_rights_t desired_rights,
                                      fbl::RefPtr<T>* dispatcher) {
    return GetDispatcherWithRights(handle_value, desired_rights, dispatcher, nullptr);
  }

  zx_koid_t GetKoidForHandle(zx_handle_t handle_value);

  bool IsHandleValid(zx_handle_t handle_value);

  // Calls the provided
  // |zx_status_t func(zx_handle_t, zx_rights_t, fbl::RefPtr<Dispatcher>)|
  // on every handle owned by the process. Stops if |func| returns an error,
  // returning the error value.
  template <typename T>
  zx_status_t ForEachHandle(T func) const {
    Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
    return ForEachHandleLocked(func);
  }

  // Similar to |ForEachHandle|, but requires the caller to be holding the |handle_table_lock_|
  template <typename T>
  zx_status_t ForEachHandleLocked(T func) const TA_REQ_SHARED(handle_table_lock_) {
    for (const auto& handle : handle_table_) {
      const Dispatcher* dispatcher = handle.dispatcher().get();
      zx_status_t s = func(MapHandleToValue(&handle), handle.rights(), dispatcher);
      if (s != ZX_OK) {
        return s;
      }
    }
    return ZX_OK;
  }

  // Iterates over every handle owned by this process and calls |func| on each one.
  //
  // Returns the error returned by |func| or ZX_OK if iteration completed without error.  Upon
  // error, iteration stops.
  //
  // |func| should match: |zx_status_t func(zx_handle_t, zx_rights_t, const Dispatcher*)|
  //
  // This method differs from ForEachHandle in that it does not hold the handle table lock for the
  // duration.  Instead, it iterates over handles in batches in order to minimize the length of time
  // the handle table lock is held.
  //
  // While the method acquires the handle table lock it does not hold the lock while calling |func|.
  // In other words, the iteration over the handle table is not atomic.  This means that the set of
  // handles |func| "sees" may be different from the set held by the process at the start or end of
  // the call.
  //
  // Handles being added or removed concurrent with |ForEachHandleBatched| may or may not be
  // observed by |func|.
  //
  // A Handle observed by |func| may or may not be owned by the process at the moment |func| is
  // invoked, however, it is guaranteed it was held at some point between the invocation of this
  // method and |func|.
  template <typename Func>
  zx_status_t ForEachHandleBatched(Func&& func);

  // accessors
  Lock<BrwLockPi>* handle_table_lock() const TA_RET_CAP(handle_table_lock_) {
    return &handle_table_lock_;
  }
  FutexContext& futex_context() { return futex_context_; }
  State state() const;
  fbl::RefPtr<VmAspace> aspace() { return aspace_; }
  fbl::RefPtr<JobDispatcher> job();

  void get_name(char out_name[ZX_MAX_NAME_LEN]) const final;
  zx_status_t set_name(const char* name, size_t len) final;

  void Kill(int64_t retcode);

  // Suspends the process.
  //
  // Suspending a process causes all child threads to suspend as well as any new children
  // that are added until the process is resumed. Suspend() is cumulative, so the process
  // will only resume once Resume() has been called an equal number of times.
  //
  // Returns ZX_OK on success, or ZX_ERR_BAD_STATE iff the process is dying or dead.
  zx_status_t Suspend();
  void Resume();

  // Syscall helpers
  void GetInfo(zx_info_process_t* info) const;
  zx_status_t GetStats(zx_info_task_stats_t* stats) const;

  // Accumulate the runtime of all threads that previously ran or are currently running under this
  // process.
  zx_status_t AccumulateRuntimeTo(zx_info_task_runtime_t* info) const;

  // NOTE: Code outside of the syscall layer should not typically know about
  // user_ptrs; do not use this pattern as an example.
  zx_status_t GetAspaceMaps(VmAspace* current_aspace, user_out_ptr<zx_info_maps_t> maps, size_t max,
                            size_t* actual, size_t* available) const;
  zx_status_t GetVmos(VmAspace* current_aspace, VmoInfoWriter& vmos, size_t max, size_t* actual,
                      size_t* available);
  zx_status_t GetThreads(fbl::Array<zx_koid_t>* threads) const;
  zx_status_t SetCriticalToJob(fbl::RefPtr<JobDispatcher> critical_to_job, bool retcode_nonzero);

  zx_status_t GetHandleInfo(fbl::Array<zx_info_handle_extended_t>* handles) const;

  Exceptionate* exceptionate(Exceptionate::Type type);

  // The following two methods can be slow and inaccurate and should only be
  // called from diagnostics code.
  uint32_t ThreadCount() const;
  size_t PageCount() const;

  // Look up a process given its koid.
  // Returns nullptr if not found.
  static fbl::RefPtr<ProcessDispatcher> LookupProcessById(zx_koid_t koid);

  // Look up a thread in this process given its koid.
  // Returns nullptr if not found.
  fbl::RefPtr<ThreadDispatcher> LookupThreadById(zx_koid_t koid);

  uintptr_t get_debug_addr() const;
  zx_status_t set_debug_addr(uintptr_t addr);

  uintptr_t get_dyn_break_on_load() const;
  zx_status_t set_dyn_break_on_load(uintptr_t break_on_load);

  // Checks |condition| and enforces the parent job's policy.
  //
  // Depending on the parent job's policy, this method may signal an exception
  // on the calling thread or signal that the current process should be
  // killed.
  //
  // Must be called by syscalls before performing an action represented by an
  // ZX_POL_xxxxx condition. If the return value is ZX_OK the action can
  // proceed; otherwise, the process is not allowed to perform the action,
  // and the status value should be returned to the usermode caller.
  //
  // E.g., in sys_channel_create:
  //
  //     auto up = ProcessDispatcher::GetCurrent();
  //     zx_status_t res = up->EnforceBasicPolicy(ZX_POL_NEW_CHANNEL);
  //     if (res != ZX_OK) {
  //         // Channel creation denied by the calling process's
  //         // parent job's policy.
  //         return res;
  //     }
  //     // Ok to create a channel.
  __WARN_UNUSED_RESULT
  zx_status_t EnforceBasicPolicy(uint32_t condition);

  // Returns this job's timer slack policy.
  TimerSlack GetTimerSlackPolicy() const;

  // return a cached copy of the vdso code address or compute a new one
  uintptr_t vdso_code_address() {
    if (unlikely(vdso_code_address_ == 0)) {
      return cache_vdso_code_address();
    }
    return vdso_code_address_;
  }

  // Retrieve the aggregated runtime of exited threads under this process.
  TaskRuntimeStats GetAggregatedRuntime() const TA_EXCL(get_lock());

 private:
  using HandleList = fbl::DoublyLinkedListCustomTraits<Handle*, Handle::NodeListTraits>;
  // HandleCursor is used to reduce the lock duration while iterate over the handle table.
  //
  // It allows iteration over the handle table to be broken up into multiple critical sections.
  class HandleCursor : public fbl::DoublyLinkedListable<HandleCursor*> {
   public:
    explicit HandleCursor(ProcessDispatcher* process);
    ~HandleCursor();

    // Invalidate this cursor.
    //
    // Once invalidated |Next| will return nullptr and |AdvanceIf| will be a no-op.
    //
    // The caller must hold the |handle_table_lock_| in Writer mode.
    void Invalidate() TA_REQ(&handle_table_lock_);

    // Advance the cursor and return the next Handle or nullptr if at the end of the list.
    //
    // Once |Next| has returned nullptr, all subsequent calls will return nullptr.
    //
    // The caller must hold the |handle_table_lock_| in Reader mode.
    Handle* Next() TA_REQ_SHARED(&handle_table_lock_);

    // If the next element is |h|, advance the cursor past it.
    //
    // The caller must hold the |handle_table_lock_| in Writer mode.
    void AdvanceIf(const Handle* h) TA_REQ(&handle_table_lock_);

   private:
    HandleCursor(const HandleCursor&) = delete;
    HandleCursor& operator=(const HandleCursor&) = delete;
    HandleCursor(HandleCursor&&) = delete;
    HandleCursor& operator=(HandleCursor&&) = delete;

    ProcessDispatcher* const process_;
    ProcessDispatcher::HandleList::iterator iter_ TA_GUARDED(&handle_table_lock_);
  };

  // Get the dispatcher corresponding to this handle value, after
  // checking that this handle has the desired rights.
  // WRONG_TYPE is returned before ACCESS_DENIED, because if the
  // wrong handle was passed, evaluating its rights does not have
  // much meaning and also this aids in debugging.
  // If successful, returns the dispatcher and the rights the
  // handle currently has.
  // If |skip_policy| is true, ZX_POL_BAD_HANDLE will not be enforced.
  template <typename T>
  zx_status_t GetDispatcherWithRightsImpl(zx_handle_t handle_value, zx_rights_t desired_rights,
                                          fbl::RefPtr<T>* out_dispatcher, zx_rights_t* out_rights,
                                          bool skip_policy) {
    bool has_desired_rights;
    zx_rights_t rights;
    fbl::RefPtr<Dispatcher> generic_dispatcher;

    {
      // Scope utilized to reduce lock duration.
      Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
      Handle* handle = GetHandleLocked(handle_value, skip_policy);
      if (!handle)
        return ZX_ERR_BAD_HANDLE;

      has_desired_rights = handle->HasRights(desired_rights);
      rights = handle->rights();
      generic_dispatcher = handle->dispatcher();
    }

    fbl::RefPtr<T> dispatcher = DownCastDispatcher<T>(&generic_dispatcher);

    // Wrong type takes precedence over access denied.
    if (!dispatcher)
      return ZX_ERR_WRONG_TYPE;

    if (!has_desired_rights)
      return ZX_ERR_ACCESS_DENIED;

    *out_dispatcher = ktl::move(dispatcher);
    if (out_rights)
      *out_rights = rights;

    return ZX_OK;
  }

  // Exit the current Process. It is an error to call this on anything other than the current
  // process. Please use ExitCurrent() instead of calling this directly.
  void Exit(int64_t retcode) __NO_RETURN;

  // compute the vdso code address and store in vdso_code_address_
  uintptr_t cache_vdso_code_address();

  // The diagnostic code is allow to know about the internals of this code.
  friend void DumpProcessList();
  friend void KillProcess(zx_koid_t id);
  friend void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

  ProcessDispatcher(fbl::RefPtr<JobDispatcher> job, ktl::string_view name, uint32_t flags);

  ProcessDispatcher(const ProcessDispatcher&) = delete;
  ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;

  zx_status_t GetDispatcherInternal(zx_handle_t handle_value, fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights);

  void OnProcessStartForJobDebugger(ThreadDispatcher* t, const arch_exception_context_t* context);

  // Thread lifecycle support.
  friend class ThreadDispatcher;
  // Takes the given ThreadDispatcher and transitions it from the INITIALIZED state to a runnable
  // state (RUNNING or SUSPENDED depending on whether this process is suspended) by calling
  // ThreadDispatcher::MakeRunnable. The thread is then added to the thread_list_ for this process
  // and we transition to running if this is the initial_thread.
  zx_status_t AddInitializedThread(ThreadDispatcher* t, bool initial_thread,
                                   const ThreadDispatcher::EntryState& entry);
  void RemoveThread(ThreadDispatcher* t);

  void SetStateLocked(State) TA_REQ(get_lock());
  void FinishDeadTransition();

  // Kill all threads
  void KillAllThreadsLocked() TA_REQ(get_lock());

  // the enclosing job
  const fbl::RefPtr<JobDispatcher> job_;

  // Job that this process is critical to.
  //
  // We require that the job is the parent of this process, or an ancestor.
  fbl::RefPtr<JobDispatcher> critical_to_job_ TA_GUARDED(get_lock());
  bool retcode_nonzero_ TA_GUARDED(get_lock()) = false;

  // Policy set by the Job during Create().
  //
  // It is critical that this field is immutable as it will be accessed without synchronization.
  const JobPolicy policy_;

  uint32_t handle_rand_ = 0;

  // list of threads in this process
  fbl::DoublyLinkedList<ThreadDispatcher*> thread_list_ TA_GUARDED(get_lock());

  // our address space
  fbl::RefPtr<VmAspace> aspace_;

  // Protects |handle_table_| and handle_table_cursors_.
  // TODO(fxbug.dev/54938): Allow multiple handle table locks to be acquired at once.
  // Right now, this is required when a process closes the last handle to
  // another process, during the destruction of the handle table.
  mutable DECLARE_BRWLOCK_PI(ProcessDispatcher, lockdep::LockFlagsMultiAcquire) handle_table_lock_;
  // This process's handle table.  When removing one or more handles from this list, be sure to
  // advance or invalidate any cursors that might point to the handles being removed.
  uint32_t handle_table_count_ TA_GUARDED(handle_table_lock_) = 0;
  HandleList handle_table_ TA_GUARDED(handle_table_lock_);
  // A list of cursors that contain pointers to elements of handle_table_.
  fbl::DoublyLinkedList<HandleCursor*> handle_table_cursors_ TA_GUARDED(handle_table_lock_);

  FutexContext futex_context_;

  // our state
  State state_ TA_GUARDED(get_lock()) = State::INITIAL;

  // Suspend count; incremented on Suspend(), decremented on Resume().
  int suspend_count_ TA_GUARDED(get_lock()) = 0;

  // True if FinishDeadTransition has been called.
  // This is used as a sanity check only.
  bool completely_dead_ = false;

  // process return code
  int64_t retcode_ = 0;

  Exceptionate exceptionate_;
  Exceptionate debug_exceptionate_;

  // This is the value of _dl_debug_addr from ld.so.
  // See third_party/ulib/musl/ldso/dynlink.c.
  uintptr_t debug_addr_ TA_GUARDED(get_lock()) = 0;

  // Whether the dynamic loader should issue a debug trap when loading a shared library,
  // either initially or when running (e.g. dlopen).
  //
  // See docs/reference/syscalls/object_get_property.md
  // See third_party/ulib/musl/ldso/dynlink.c.
  uintptr_t dyn_break_on_load_ TA_GUARDED(get_lock()) = 0;

  // This is a cache of aspace()->vdso_code_address().
  uintptr_t vdso_code_address_ = 0;

  // The user-friendly process name. For debug purposes only. That
  // is, there is no mechanism to mint a handle to a process via this name.
  fbl::Name<ZX_MAX_NAME_LEN> name_;

  // Aggregated runtime stats from exited threads.
  TaskRuntimeStats aggregated_runtime_stats_ TA_GUARDED(get_lock());
};

const char* StateToString(ProcessDispatcher::State state);

template <typename Func>
zx_status_t ProcessDispatcher::ForEachHandleBatched(Func&& func) {
  HandleCursor cursor(this);

  bool done = false;
  while (!done) {
    struct Args {
      zx_handle_t handle_value;
      zx_rights_t desired_rights;
      // Use a RefPtr to ensure the dispatcher isn't destroyed out from under |func|.
      fbl::RefPtr<const Dispatcher> dispatcher;
    };
    // The smaller this value is, the more we'll acquire/release the handle table lock.  The larger
    // it is, the longer the duration we'll hold the lock.  This value also impacts the required
    // stack size.
    static constexpr size_t kMaxBatchSize = 64;
    ktl::array<Args, kMaxBatchSize> batch{};

    // Don't use too much stack space.  The limit here is somewhat arbitrary.
    static_assert(sizeof(batch) <= 1024);

    // Gather a batch of arguments while holding the handle table lock.
    size_t count = 0;
    {
      Guard<BrwLockPi, BrwLockPi::Reader> guard{&handle_table_lock_};
      for (; count < kMaxBatchSize; ++count) {
        Handle* handle = cursor.Next();
        if (!handle) {
          done = true;
          break;
        }
        batch[count] = {MapHandleToValue(handle), handle->rights(), handle->dispatcher()};
      }
    }

    // Now that we have a batch of handles, call |func| on each one.
    for (size_t i = 0; i < count; ++i) {
      zx_status_t status = ktl::forward<Func>(func)(batch[i].handle_value, batch[i].desired_rights,
                                                    batch[i].dispatcher.get());
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  return ZX_OK;
}

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PROCESS_DISPATCHER_H_
