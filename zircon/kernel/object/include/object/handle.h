// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/gparena.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <kernel/event_limiter.h>
#include <ktl/atomic.h>
#include <ktl/move.h>

class Dispatcher;
class Handle;

constexpr uint32_t kHandleReservedBits = 2;

template <typename T>
class KernelHandle;

// Callable object for destroying uniquely owned handles.
struct HandleDestroyer {
  void operator()(Handle*);
};

// HandleOwner wraps a Handle in a unique_ptr that has single
// ownership of the Handle and deletes it whenever it falls out of scope.
using HandleOwner = ktl::unique_ptr<Handle, HandleDestroyer>;

class HandleTableArena;

// A Handle is how a specific process refers to a specific Dispatcher.
class Handle final {
 public:
  // Returns the Dispatcher to which this instance points.
  const fbl::RefPtr<Dispatcher>& dispatcher() const { return dispatcher_; }

  // Returns the process that owns this instance. Used to guarantee
  // that one process may not access a handle owned by a different process.
  zx_koid_t process_id() const { return process_id_.load(ktl::memory_order_relaxed); }

  // Sets the value returned by process_id().
  void set_process_id(zx_koid_t pid);

  // Returns the |rights| parameter that was provided when this instance
  // was created.
  uint32_t rights() const { return rights_; }

  // Returns true if this handle has all of the desired rights bits set.
  bool HasRights(zx_rights_t desired) const { return (rights_ & desired) == desired; }

  // Returns a value that can be decoded by Handle::FromU32() to derive a
  // pointer to this instance.  ProcessDispatcher will XOR this with its
  // |handle_rand_| to create the zx_handle_t value that user space sees.
  uint32_t base_value() const { return base_value_; }

  // To be called once during bring up.
  static void Init();

  // Maps an integer obtained by Handle::base_value() back to a Handle.
  static Handle* FromU32(uint32_t value);

  // Get the number of outstanding handles for a given dispatcher.
  static uint32_t Count(const fbl::RefPtr<const Dispatcher>&);

  // Should only be called by diagnostics.cpp.
  struct diagnostics {
    // Dumps internal details of the handle table using printf().
    static void DumpTableInfo();

    // Returns the number of outstanding handles.
    static size_t OutstandingHandles();
  };

  // Handle should never be created by anything other than Make or Dup.
  static HandleOwner Make(fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights);
  static HandleOwner Make(KernelHandle<Dispatcher> kernel_handle, zx_rights_t rights);
  static HandleOwner Dup(Handle* source, zx_rights_t rights);

  // Use a manually declared linked list node state instead of inheriting so that the early
  // memory of the class can be used by the members we want to preserve, and our NodeState can
  // be placed later.
  using NodeState = fbl::DoublyLinkedListNodeState<Handle*>;
  struct NodeListTraits {
    static NodeState& node_state(Handle& h) { return h.node_state_; }
  };

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Handle);

  // Called only by Make.
  Handle(fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights, uint32_t base_value);
  // Called only by Dup.
  Handle(Handle* rhs, zx_rights_t rights, uint32_t base_value);

  // Handle should never be destroyed by anything other than HandleTableArena::Delete,
  // which uses TearDown to do the actual destruction.
  ~Handle() = default;
  void TearDown();

  // NOTE! This can return an invalid address.  It must be checked
  // against the arena bounds before being cast to a Handle*.
  static uintptr_t IndexToHandle(uint32_t index);

  // process_id_ is atomic because threads from different processes can
  // access it concurrently, while holding different instances of
  // handle_table_lock_.
  ktl::atomic<zx_koid_t> process_id_;
  fbl::RefPtr<Dispatcher> dispatcher_;
  const zx_rights_t rights_;
  const uint32_t base_value_;

  // Up to here the members need to be preserved when handles are free'd to the arena. The
  // PreserveSize is an 'approximation' of how large all the previous members are, but we make
  // 'HandleTableArena' a friend so that it can statically validate that the chosen PreserveSize
  // is correct. Any incorrect size will result in a compilation error!
  static constexpr size_t PreserveSize = 24;
  friend HandleTableArena;

  NodeState node_state_;
};

class HandleTableArena {
 public:
  // Alloc returns storage for a handle.
  void* Alloc(const fbl::RefPtr<Dispatcher>&, const char* what, uint32_t* base_value);

  void Delete(Handle* handle);

 private:
  // GetNewBaseValue is a helper needed to actually create a Handle.
  uint32_t GetNewBaseValue(void* addr);

  // A helper for the GetNewBaseValue computation.
  uint32_t HandleToIndex(Handle* handle);

  // Validate that all the fields we need to preserve fit within the preservation window.
  static_assert(offsetof(Handle, process_id_) + sizeof(Handle::process_id_) <=
                Handle::PreserveSize);
  static_assert(offsetof(Handle, base_value_) + sizeof(Handle::base_value_) <=
                Handle::PreserveSize);
  static_assert(offsetof(Handle, dispatcher_) + sizeof(Handle::dispatcher_) <=
                Handle::PreserveSize);
  fbl::GPArena<Handle::PreserveSize, sizeof(Handle)> arena_;

  // Limit logs about handle counts being too high.
  EventLimiter<ZX_SEC(1)> handle_count_high_log_;

  // Give the Handle access to its arena.
  friend Handle;
};

extern HandleTableArena gHandleTableArena;

// This can't be defined directly in the HandleDestroyer struct definition
// because Handle is an incomplete type at that point.
inline void HandleDestroyer::operator()(Handle* handle) {
  // ktl::unique_ptr only calls its deleter when the pointer is non-null.
  // Still, we double check.
  DEBUG_ASSERT(handle != nullptr);
  gHandleTableArena.Delete(handle);
}

// A minimal wrapper around a Dispatcher which is owned by the kernel.
//
// Intended usage when creating new a Dispatcher object is:
//   1. Create a KernelHandle on the stack (cannot fail)
//   2. Move the RefPtr<Dispatcher> into the KernelHandle (cannot fail)
//   3. When ready to give the handle to a process, upgrade the KernelHandle
//      to a full HandleOwner via UpgradeToHandleOwner() or
//      user_out_handle::make() (can fail)
//
// This sequence ensures that the Dispatcher's on_zero_handles() method is
// called even if errors occur during or before HandleOwner creation, which
// is necessary to break circular references for some Dispatcher types.
//
// This class is thread-unsafe and must be externally synchronized if used
// across multiple threads.
template <typename T>
class KernelHandle {
 public:
  KernelHandle() = default;

  // |dispatcher|'s handle count must be 0.
  explicit KernelHandle(fbl::RefPtr<T> dispatcher) : dispatcher_(ktl::move(dispatcher)) {
    DEBUG_ASSERT(!dispatcher_ || dispatcher_->current_handle_count() == 0);
  }

  ~KernelHandle() { reset(); }

  // Movable but not copyable since we own the underlying Dispatcher.
  KernelHandle(const KernelHandle&) = delete;
  KernelHandle& operator=(const KernelHandle&) = delete;

  template <typename U>
  KernelHandle(KernelHandle<U>&& other) : dispatcher_(ktl::move(other.dispatcher_)) {}

  template <typename U>
  KernelHandle& operator=(KernelHandle<U>&& other) {
    reset(ktl::move(other.dispatcher_));
    return *this;
  }

  void reset() { reset(fbl::RefPtr<T>()); }

  template <typename U>
  void reset(fbl::RefPtr<U> dispatcher) {
    if (dispatcher_) {
      dispatcher_->on_zero_handles();
    }
    dispatcher_ = ktl::move(dispatcher);
  }

  const fbl::RefPtr<T>& dispatcher() const { return dispatcher_; }

  fbl::RefPtr<T> release() { return ktl::move(dispatcher_); }

 private:
  template <typename U>
  friend class KernelHandle;

  fbl::RefPtr<T> dispatcher_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_HANDLE_H_
