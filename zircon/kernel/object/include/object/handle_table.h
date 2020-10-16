// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_TABLE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_TABLE_H_

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

// A HandleTable is the data structure which associates a Handle to a
// particular ProcessDispatcher. Each HandleTable is permanently
// associated with a single ProcessDispatcher.

class ProcessDispatcher;

class HandleTable {
 public:
  explicit HandleTable(ProcessDispatcher* process) __NONNULL((2));
  ~HandleTable();

  HandleTable(const HandleTable&) = delete;
  HandleTable(HandleTable&&) = delete;

  HandleTable& operator=(const HandleTable&) = delete;
  HandleTable& operator=(HandleTable&&) = delete;

  // Maps a |handle| to an integer which can be given to usermode as a
  // handle value. Uses Handle->base_value() plus additional mixing.
  zx_handle_t MapHandleToValue(const Handle* handle) const;
  zx_handle_t MapHandleToValue(const HandleOwner& handle) const;

  // Maps a handle value into a Handle as long we can verify that
  // it belongs to this handle table. Use |skip_policy = true| for testing that
  // a handle is valid without potentially triggering a job policy exception.
  Handle* GetHandleLocked(zx_handle_t handle_value, bool skip_policy = false)
      TA_REQ_SHARED(handle_table_lock_);

  // Returns the number of outstanding handles in this handle table.
  uint32_t HandleCount() const;

  // Adds |handle| to this handle table. The handle->process_id() is
  // set to process_'s koid.
  void AddHandle(HandleOwner handle);
  void AddHandleLocked(HandleOwner handle) TA_REQ(handle_table_lock_);

  // Set of overloads that remove the |handle| or |handle_value| from this
  // handle table and returns ownership to the handle.
  HandleOwner RemoveHandleLocked(Handle* handle) TA_REQ(handle_table_lock_);
  HandleOwner RemoveHandleLocked(zx_handle_t handle_value) TA_REQ(handle_table_lock_);
  HandleOwner RemoveHandle(zx_handle_t handle_value);

  // Remove all of an array of |handles| from the handle table. Returns ZX_OK if all of the
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
  // on every handle owned by the handle table. Stops if |func| returns an error,
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

  // Iterates over every handle owned by this handle table and calls |func| on each one.
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
  // handles |func| "sees" may be different from the set held by the handle table at the start or
  // end of the call.
  //
  // Handles being added or removed concurrent with |ForEachHandleBatched| may or may not be
  // observed by |func|.
  //
  // A Handle observed by |func| may or may not be owned by the handle table at the moment |func| is
  // invoked, however, it is guaranteed it was held at some point between the invocation of this
  // method and |func|.
  template <typename Func>
  zx_status_t ForEachHandleBatched(Func&& func);

  zx_status_t GetHandleInfo(fbl::Array<zx_info_handle_extended_t>* handles) const;

  // Called when the containing ProcessDispatcher transitions to the Dead state.
  void Clean();

  // accessors
  Lock<BrwLockPi>* handle_table_lock() const TA_RET_CAP(handle_table_lock_) {
    return &handle_table_lock_;
  }

 private:
  using HandleList = fbl::DoublyLinkedListCustomTraits<Handle*, Handle::NodeListTraits>;
  // HandleCursor is used to reduce the lock duration while iterate over the handle table.
  //
  // It allows iteration over the handle table to be broken up into multiple critical sections.
  class HandleCursor : public fbl::DoublyLinkedListable<HandleCursor*> {
   public:
    explicit HandleCursor(HandleTable* process);
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

    HandleTable* const handle_table_;
    HandleTable::HandleList::iterator iter_ TA_GUARDED(&handle_table_lock_);
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

  zx_status_t GetDispatcherInternal(zx_handle_t handle_value, fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights);

  // Protects |handle_table_| and |handle_table_cursors_|.
  // TODO(fxbug.dev/54938): Allow multiple handle table locks to be acquired at once.
  // Right now, this is required when a process closes the last handle to
  // another process, during the destruction of the handle table.
  mutable DECLARE_BRWLOCK_PI(HandleTable, lockdep::LockFlagsMultiAcquire) handle_table_lock_;

  // Each handle table provides pseudorandom userspace handle
  // values. This is the per-handle-table pseudorandom state.
  uint32_t handle_rand_ = 0;

  // The actual handle table.  When removing one or more handles from this list, be sure to
  // advance or invalidate any cursors that might point to the handles being removed.
  uint32_t handle_table_count_ TA_GUARDED(handle_table_lock_) = 0;
  HandleList handle_table_ TA_GUARDED(handle_table_lock_);

  // The containing ProcessDispatcher.
  ProcessDispatcher* const process_;

  // A list of cursors that contain pointers to elements of handle_table_.
  fbl::DoublyLinkedList<HandleCursor*> handle_table_cursors_ TA_GUARDED(handle_table_lock_);
};

template <typename Func>
zx_status_t HandleTable::ForEachHandleBatched(Func&& func) {
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

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_TABLE_H_
