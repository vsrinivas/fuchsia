// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_OBJECT_CACHE_INCLUDE_LIB_OBJECT_CACHE_H_
#define ZIRCON_KERNEL_LIB_OBJECT_CACHE_INCLUDE_LIB_OBJECT_CACHE_H_

#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/zx/result.h>
#include <stdint.h>
#include <trace.h>
#include <zircon/errors.h>

#include <new>
#include <type_traits>

#include <arch/kernel_aspace.h>
#include <arch/ops.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/canary.h>
#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <ktl/array.h>
#include <ktl/bit.h>
#include <ktl/forward.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/unique_ptr.h>
#include <vm/page_state.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

// ObjectCache is a power of two slab allocator. Slabs are allocated and
// retained for future use up to the specified limit, reducing contention on the
// the underlying allocator. A variant with per-CPU slab caches is provided to
// further improve concurrency in high-demand use cases.
//
// This allocator supports back reference lifetime dependency in ref counted
// types, where ObjectCache allocated objects hold ref pointers to the object
// that owns the ObjectCache the objects are allocated from.
//
// For example, the ref counted object Parent allocates ref counted Child
// objects with references back to itself:
//
//   struct Parent : fbl::RefCountable<Parent> {
//     struct Child : fbl::RefCountable<Child> {
//       fbl::RefPtr<Parent> parent;
//     };
//
//     // The lifetime of this allocator is bounded by the lifetime of the Parent
//     // containing it.
//     object_cache::ObjectCache<Child> allocator;
//
//     zx::result<fbl::RefPtr<Child>> Allocate() {
//       zx::result result = allocator.Allocate(fbl::RefPtr{this});
//       if (result.is_error()) {
//         return result.take_error();
//       }
//       return zx::ok(fbl::AdoptRef(result.value().release()));
//     }
//   };
//
// Assume the following allocations succeed:
//
//   fbl::RefPtr parent = fbl::AdoptRef(new Parent{});
//   fbl::RefPtr child_a = fbl::AdoptRef(parent->Allocate().value());
//   fbl::RefPtr child_b = fbl::AdoptRef(parent->Allocate().value());
//
// The ref pointers parent, child_a, and child_b can be released in any order,
// even concurrently. ObjectCache manages the lifetimes of the slabs the Child
// instances are allocated from to ensure the memory is valid until the last
// Child is destroyed, even if that is after the ObjectCache has been destroyed.
//

namespace object_cache {

// The default allocator for the object cache. Allocates page sized slabs from
// the PMM. This may be replaced by a higher-order page allocator without loss
// of generality.
struct DefaultAllocator {
  static constexpr size_t kSlabSize = PAGE_SIZE;

  static zx::result<void*> Allocate() {
    paddr_t paddr = 0;
    vm_page_t* vm_page;
    const zx_status_t page_alloc_status = pmm_alloc_page(PMM_ALLOC_FLAG_ANY, &vm_page, &paddr);
    if (page_alloc_status != ZX_OK) {
      return zx::error_result(page_alloc_status);
    }

    vm_page->set_state(vm_page_state::SLAB);
    return zx::ok(paddr_to_physmap(paddr));
  }

  static void Release(void* slab) {
    vm_page_t* page = paddr_to_vm_page(physmap_to_paddr(slab));
    DEBUG_ASSERT(page->state() == vm_page_state::SLAB);
    pmm_free_page(page);
  }

  static void CountObjectAllocation();
  static void CountObjectFree();
  static void CountSlabAllocation();
  static void CountSlabFree();
};

// Selects between single cache and per-CPU cache modes when instantiating
// ObjectCache.
enum class Option { Single, PerCpu };

// Base type. Specializations handle single cache and per-CPU cache variants.
template <typename T, Option = Option::Single, typename Allocator = DefaultAllocator>
class ObjectCache;

// Mixin type that provides a delete operator that returns the deleted object to
// the object cache it was allocated from.
//
// Example usage:
//
//   class Foo : public object_cache::Deletable<Foo> { ... };
//
template <typename T, typename Allocator = DefaultAllocator>
struct Deletable {
  static void operator delete(void* object, size_t size) {
    ObjectCache<T, Option::Single, Allocator>::Delete(static_cast<T*>(object));
  }
};

// Functor type that frees the given object using the object cache it was
// allocated from.
//
// Example usage:
//
//   ktl::unique_ptr<Foo, object_cache::Deleter<Foo>> ptr;
//
template <typename T, typename Allocator = DefaultAllocator>
struct Deleter {
  using Type = std::remove_const_t<T>;
  void operator()(const Type* object) const {
    Type* pointer = const_cast<Type*>(object);
    pointer->Type::~Type();
    ObjectCache<Type, Option::Single, Allocator>::Delete(pointer);
  }
};

// Simplified type alias of ktl::unique_ptr with the appropriate deleter type.
template <typename T, typename Allocator = DefaultAllocator>
using UniquePtr = ktl::unique_ptr<T, Deleter<T, Allocator>>;

// The maximum size of the slab control block. Custom allocators may use this
// constant to compute slab sizes, taking into account the size of the control
// block and the desired number of objects per slab.
static constexpr size_t kSlabControlMaxSize = 144;

// Specialization of ObjectCache for the single slab cache variant. Operations
// serialize on the main object cache lock, regardless of CPU.
template <typename T, typename Allocator>
class ObjectCache<T, Option::Single, Allocator> {
  template <typename Return, typename... Args>
  using EnableIfConstructible = std::enable_if_t<std::is_constructible_v<T, Args...>, Return>;

  static constexpr int kTraceLevel = 0;

  using Basic = TraceEnabled<(kTraceLevel > 0)>;
  using Detail = TraceEnabled<(kTraceLevel > 1)>;

  template <typename EnabledOption>
  using LocalTraceDuration =
      TraceDuration<EnabledOption, KTRACE_GRP_SCHEDULER, TraceContext::Thread>;

  static_assert(ktl::has_single_bit(Allocator::kSlabSize), "Slabs must be a power of two!");
  static constexpr uintptr_t kSlabAddrMask = Allocator::kSlabSize - 1;

 public:
  // Constructs an ObjectCache with the given slab reservation value. Reserve
  // slabs are not immediately allocated.
  explicit ObjectCache(size_t reserve_slabs) : reserve_slabs_{reserve_slabs} {
    LocalTraceDuration<Detail> trace{"ObjectCache::ObjectCache"_stringref};
  }

  ~ObjectCache() {
    LocalTraceDuration<Detail> trace{"ObjectCache::~ObjectCache"_stringref};

    {
      Guard<Mutex> guard{&lock_};
      // Mark active slabs orphan. Threads racing in Slab::Free may not observe
      // this state before attempting to acquire the cache lock.
      for (Slab& slab : full_list_) {
        slab.SetOrphan();
      }
      for (Slab& slab : partial_list_) {
        slab.SetOrphan();
      }
    }

    // Wait for any threads racing in Slab::Free to release their slab locks to
    // ensure that the cache lock is not destroyed while they hold it.
    for (Slab& slab : full_list_) {
      Guard<Mutex> guard{&slab.control.lock};
    }
    for (Slab& slab : partial_list_) {
      Guard<Mutex> guard{&slab.control.lock};
    }
  }

  // ObjectCache is not copiable.
  ObjectCache(const ObjectCache&) = delete;
  ObjectCache& operator=(const ObjectCache&) = delete;

  // ObjectCache is not movable.
  ObjectCache(ObjectCache&& other) = delete;
  ObjectCache& operator=(ObjectCache&& other) = delete;

  using PtrType = UniquePtr<T, Allocator>;

  // Allocates an instance of T from a slab and constructs it with the given
  // arguments. Returns a pointer to the constructed object as a unique pointer.
  // If the object is ref counted it is not yet adopted.
  template <typename... Args>
  EnableIfConstructible<zx::result<PtrType>, Args...> Allocate(Args&&... args) TA_EXCL(lock_) {
    LocalTraceDuration<Basic> trace{"ObjectCache::Allocate"_stringref};
    DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
    AutoPreemptDisabler preempt_disable;

    while (true) {
      zx::result<SlabPtr> slab = GetSlab();
      if (slab.is_error()) {
        return slab.take_error();
      }

      // Allocation can fail if one or more allocating threads race with a
      // thread that is either filling or releasing a slab. Retry on another
      // slab until allocation succeeds or slab allocation fails due to
      // insufficient memory.
      zx::result<Entry*> object = slab->Allocate();
      if (object.is_ok()) {
        return zx::ok(object.value()->ToObject(ktl::forward<Args>(args)...));
      }
    }
  }

  size_t slab_count() const {
    Guard<Mutex> guard{&lock_};
    return slab_count_;
  }

  static constexpr size_t objects_per_slab() { return kEntriesPerSlab; }

 private:
  template <typename, typename>
  friend struct Deleter;
  template <typename, typename>
  friend struct Deletable;

  // Predicate indicating whether the number of slabs is greater than the
  // reserve value.
  bool should_trim() const TA_REQ(lock_) { return slab_count_ > reserve_slabs_; }

  // Returns the given object that has already been destroyed to the slab it was
  // allocated from.
  static void Delete(void* pointer) {
    LocalTraceDuration<Basic> trace{"ObjectCache::Release"_stringref};
    AutoPreemptDisabler preempt_disable;
    SlabPtr slab = Slab::FromAllocatedPointer(pointer);
    Entry* entry = Entry::ToListNode(pointer);
    slab->Free(entry);
  }

  // Optimization flags that impact the efficiency of the Slab destructor. May
  // be set to fbl::NodeOptionsNone to force manually clearing the free list for
  // debugging purposes.
  static constexpr fbl::NodeOptions kEntryNodeOptions = fbl::NodeOptions::AllowClearUnsafe;

  // An entry in the Slab object array. Exists either as a node in the slab free
  // list or as an allocated instance of T.
  union Entry {
    using NodeState = fbl::DoublyLinkedListNodeState<Entry*, kEntryNodeOptions>;
    static NodeState& node_state(Entry& entry) { return entry.list_node; }

    Entry() : list_node{} {}
    ~Entry() { list_node.~NodeState(); }

    // Converts this entry from a list node to an object, constructing the
    // instance of T with the given arguments. This should be called after the
    // entry is allocated, outside of internal locks to avoid unnecessary lock
    // dependencies.
    template <typename... Args>
    T* ToObject(Args&&... args) {
      LocalTraceDuration<Detail> trace{"Entry::ToObject"_stringref};
      list_node.~NodeState();
      new (&object) T(ktl::forward<Args>(args)...);
      return &object;
    }

    // Converts this entry to a list node. The object must already be destroyed.
    static Entry* ToListNode(void* pointer) {
      LocalTraceDuration<Detail> trace{"Entry::ToListNode"_stringref};
      Entry* entry = static_cast<Entry*>(pointer);
      new (&entry->list_node) NodeState{};
      return entry;
    }

    NodeState list_node;
    T object;
  };
#ifdef __clang__
  static_assert(offsetof(Entry, object) == 0);
#endif

  struct Slab;
  using SlabPtr = fbl::RefPtr<Slab>;
  using SlabList = fbl::DoublyLinkedListCustomTraits<SlabPtr, Slab>;

  // Slab control block. Separate from the defintion of Slab to simplify
  // computing the size of the Entry array.
  struct SlabControl {
    explicit SlabControl(ObjectCache* object_cache) : object_cache{object_cache} {}

    fbl::Canary<fbl::magic("slab")> canary;
    fbl::RefCounted<SlabControl> ref_count;
    fbl::DoublyLinkedListNodeState<SlabPtr> list_node;
    ktl::atomic<bool> orphan_flag{false};

    DECLARE_MUTEX(SlabControl) lock;

    TA_GUARDED(lock)
    ObjectCache* object_cache;
    TA_GUARDED(lock)
    fbl::DoublyLinkedListCustomTraits<Entry*, Entry, fbl::SizeOrder::Constant> free_list;
  };
  static_assert(sizeof(SlabControl) <= kSlabControlMaxSize);

  static constexpr ssize_t kEntriesPerSlab =
      (Allocator::kSlabSize - fbl::round_up(sizeof(SlabControl), alignof(Entry))) / sizeof(Entry);
  static_assert(kEntriesPerSlab > 0);

  // A slab of objects in the object cache. Constructed on a raw block of power
  // of two aligned memory.
  struct Slab {
    explicit Slab(ObjectCache* object_cache) : control{object_cache} {
      LocalTraceDuration<Detail> trace{"Slab::Slab"_stringref};
      for (Entry& entry : entries) {
        control.free_list.push_front(&entry);
      }
    }

    ~Slab() {
      LocalTraceDuration<Detail> trace{"Slab::~Slab"_stringref};
      DEBUG_ASSERT(is_empty());
      if constexpr (kEntryNodeOptions & fbl::NodeOptions::AllowClearUnsafe) {
        control.free_list.clear_unsafe();
      } else {
        // Consistency check that every entry is on the free list. Attempting to
        // erase an entry that is not on the free list will assert.
        for (Entry& entry : entries) {
          control.free_list.erase(entry);
        }
      }
    }

    // Returns the raw memory for the slab to the allocator when the last
    // reference is released.
    static void operator delete(void* slab, size_t size) {
      LocalTraceDuration<Detail> trace{"Slab::delete"_stringref};
      DEBUG_ASSERT(size == sizeof(Slab));
      Allocator::CountSlabFree();
      Allocator::Release(slab);
    }

    // Forward reference counting methods to the control block.
    void AddRef() const { control.ref_count.AddRef(); }
    bool Release() const __WARN_UNUSED_RESULT { return control.ref_count.Release(); }
    void Adopt() const { return control.ref_count.Adopt(); }
    int ref_count_debug() const { return control.ref_count.ref_count_debug(); }

    // Forward slab list node methods from the control block.
    bool InContainer() { return control.list_node.InContainer(); }

    static auto& node_state(Slab& slab) { return slab.control.list_node; }

    void SetOrphan() {
      LocalTraceDuration<Detail> trace{"Slab::SetOrphan"_stringref};
      control.orphan_flag = true;
    }

    size_t available_objects() const TA_REQ(control.lock) { return control.free_list.size(); }
    bool is_empty() const TA_REQ(control.lock) { return available_objects() == kEntriesPerSlab; }
    bool is_full() const TA_REQ(control.lock) { return control.free_list.is_empty(); }
    bool is_orphan() const { return control.orphan_flag; }

    // Returns a reference to a slab given a pointer to an entry.
    static SlabPtr FromAllocatedPointer(void* pointer) {
      // Slab addresses are guaranteed to be power-of-two aligned. This contract
      // is checked in AllocateSlab.
      const uintptr_t addr = reinterpret_cast<uintptr_t>(pointer) & ~kSlabAddrMask;
      Slab* slab = reinterpret_cast<Slab*>(addr);
      slab->control.canary.Assert();
      return fbl::RefPtr{slab};
    }

    // Allocates an entry from the slab and moves the slab to the appropriate
    // list in the cache. The caller must hold a reference to this slab.
    zx::result<Entry*> Allocate() {
      LocalTraceDuration<Detail> trace{"Slab::Allocate"_stringref};

      Guard<Mutex> slab_guard{&control.lock};
      DEBUG_ASSERT(!is_orphan());

      // Retry on another slab if another thread allocated the last object in
      // this slab between releasing the object cache lock in GetSlab and
      // acquiring the slab lock here.
      if (available_objects() == 0) {
        return zx::error_result(ZX_ERR_NO_MEMORY);
      }

      Guard<Mutex> object_cache_guard{&control.object_cache->lock_};

      // Retry on another slab if another thread removed this slab from the
      // object cache between releasing the object cache lock in GetSlab and
      // acquiring the slab lock here. Technically, this slab could be re-added
      // to the cache, however, there is a good chance that another partial slab
      // exists that would reduce fragmentation.
      if (!InContainer()) {
        return zx::error_result(ZX_ERR_NO_MEMORY);
      }

      const bool was_empty = is_empty();
      Entry* entry = control.free_list.pop_front();

      if (was_empty || is_full()) {
        SlabList& from_list =
            was_empty ? control.object_cache->empty_list_ : control.object_cache->partial_list_;
        SlabList& to_list =
            is_full() ? control.object_cache->full_list_ : control.object_cache->partial_list_;
        to_list.push_front(from_list.erase(*this));
      }

      // The allocated object maintains a reference to prevent prematurely
      // releasing the slab in back reference scenarios.
      AddRef();

      Allocator::CountObjectAllocation();
      return zx::ok(entry);
    }

    // Returns the given entry to the free list. The caller must hold a
    // reference to the slab.
    void Free(void* pointer) {
      LocalTraceDuration<Detail> trace{"Slab::Free"_stringref};
      Entry* entry = reinterpret_cast<Entry*>(pointer);
      DEBUG_ASSERT(entry >= entries.begin() && entry < entries.end());

      Guard<Mutex> control_guard{&control.lock};
      DEBUG_ASSERT(available_objects() < kEntriesPerSlab);

      // If the cache containing this slab was destroyed while destroying the
      // last object in the cache (e.g. back reference) this slab will be marked
      // orphan and should not attempt to access the object cache.
      if (is_orphan()) {
        // Just return the entry to the free list and release the additional
        // reference to the orphan slab.
        control.free_list.push_front(entry);
      } else {
        control.object_cache->canary_.Assert();
        Guard<Mutex> guard{&control.object_cache->lock_};

        const bool was_full = is_full();
        control.free_list.push_front(entry);

        // This slab may have been orphaned while blocking on the cache lock
        // above if the cache destructor ran concurrently with this free
        // operation.
        if (!is_orphan()) {
          if (was_full || is_empty()) {
            SlabList& from_list =
                was_full ? control.object_cache->full_list_ : control.object_cache->partial_list_;
            SlabList& to_list = is_empty() ? control.object_cache->empty_list_
                                           : control.object_cache->partial_list_;
            to_list.push_front(from_list.erase(*this));
          }

          if (is_empty() && control.object_cache->should_trim()) {
            control.object_cache->RemoveSlab(this);
          }
        }
      }

      Allocator::CountObjectFree();

      // Release the freed object's reference to the slab.
      [[maybe_unused]] const bool should_release = Release();
      DEBUG_ASSERT(should_release == false);
    }

    SlabControl control;
    ktl::array<Entry, kEntriesPerSlab> entries{};
  };
#ifdef __clang__
  static_assert(offsetof(Slab, control) == 0);
#endif
  static_assert(sizeof(Slab) <= Allocator::kSlabSize);

  // Returns a reference to a slab with at least one available entry. Allocates
  // a new slab if no slabs have available entries.
  zx::result<SlabPtr> GetSlab() TA_EXCL(lock_) {
    LocalTraceDuration<Detail> trace{"ObjectCache::GetSlab"_stringref};
    Guard<Mutex> guard{&lock_};

    if (partial_list_.is_empty() && empty_list_.is_empty()) {
      return AllocateSlab();
    }

    Slab& slab = partial_list_.is_empty() ? empty_list_.front() : partial_list_.front();
    return zx::ok(fbl::RefPtr{&slab});
  }

  // Allocates a new slab and adds it to the empty list.
  zx::result<Slab*> AllocateSlab() TA_REQ(lock_) {
    LocalTraceDuration<Detail> trace{"ObjectCache::AllocateSlab"_stringref};

    zx::result<void*> result = Allocator::Allocate();
    if (result.is_ok()) {
      Allocator::CountSlabAllocation();

      void* const pointer = result.value();
      DEBUG_ASSERT((reinterpret_cast<uintptr_t>(pointer) & kSlabAddrMask) == 0);

      Slab* slab = new (pointer) Slab{this};

      empty_list_.push_front(fbl::AdoptRef(slab));
      slab_count_++;

      return zx::ok(slab);
    }

    return result.take_error();
  }

  // Removes the given slab from this cache.
  void RemoveSlab(Slab* slab) TA_REQ(lock_) {
    LocalTraceDuration<Detail> trace{"ObjectCache::RemoveSlab"_stringref};
    empty_list_.erase(*slab);
    slab_count_--;
  }

  fbl::Canary<fbl::magic("obj$")> canary_;
  const size_t reserve_slabs_;

  mutable DECLARE_MUTEX(ObjectCache) lock_;
  size_t slab_count_ TA_GUARDED(lock_){0};

  // Lists of slabs in the object cache with the following functions:
  //  - The partial list contains slabs with some allocated objects and some
  //    entries in the free list.
  //  - The empty list contains retained slabs with no allocated objects and all
  //    entries in the free list.
  //  - The full list containing slabs with all objects allocated and no entries
  //    in the free list.
  //
  // Allocation from slabs in the partial list is preferred over the empty list
  // to reduce fragmentation.
  //
  SlabList partial_list_ TA_GUARDED(lock_);
  SlabList empty_list_ TA_GUARDED(lock_);
  SlabList full_list_ TA_GUARDED(lock_);
};

namespace internal {

size_t GetProcessorCount();

}  // namespace internal

// Specialization of ObjectCache for the per-CPU slab cache variant. Operations
// serialize on per-CPU object cache locks.
template <typename T, typename Allocator>
class ObjectCache<T, Option::PerCpu, Allocator> {
  template <typename Return, typename... Args>
  using EnableIfConstructible = std::enable_if_t<std::is_constructible_v<T, Args...>, Return>;

 public:
  // ObjectCache is default constructible in the empty state.
  ObjectCache() = default;

  // ObjectCache is not copiable.
  ObjectCache(const ObjectCache&) = delete;
  ObjectCache& operator=(const ObjectCache&) = delete;

  // ObjectCache is movable.
  ObjectCache(ObjectCache&&) noexcept = default;
  ObjectCache& operator=(ObjectCache&&) noexcept = default;

  // Creates a per-CPU ObjectCache with the given slab reservation value. The
  // reserve value applies to each per-CPU cache independently. Reserve slabs
  // are not immediately allocated.
  static zx::result<ObjectCache> Create(size_t reserve_slabs) {
    const size_t processor_count = internal::GetProcessorCount();
    fbl::AllocChecker checker;
    ktl::unique_ptr<CpuCache[]> per_cpu_caches{new (&checker) CpuCache[processor_count]};
    if (!checker.check()) {
      return zx::error_result(ZX_ERR_NO_MEMORY);
    }

    for (size_t i = 0; i < processor_count; i++) {
      per_cpu_caches[i].emplace(reserve_slabs);
    }

    return zx::ok(ObjectCache{processor_count, ktl::move(per_cpu_caches)});
  }

  using PtrType = typename ObjectCache<T, Option::Single, Allocator>::PtrType;

  // Allocates an instance of T from a slab, using the object cache of the
  // current CPU, and constructs it with the given arguments. Returns a pointer
  // to the constructed object as a unique pointer. If the object is ref counted
  // it is not yet adopted.
  template <typename... Args>
  EnableIfConstructible<zx::result<PtrType>, Args...> Allocate(Args&&... args) {
    DEBUG_ASSERT(cpu_caches_ != nullptr);
    DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());

    AutoPreemptDisabler preempt_disable;
    const cpu_num_t current_cpu = arch_curr_cpu_num();
    DEBUG_ASSERT(current_cpu < processor_count_);
    return cpu_caches_[current_cpu]->Allocate(ktl::forward<Args>(args)...);
  }

  size_t slab_count() const {
    size_t count = 0;
    for (size_t i = 0; i < processor_count_; i++) {
      count += cpu_caches_[i]->slab_count();
    }
    return count;
  }

 private:
  using CpuCache = ktl::optional<ObjectCache<T, Option::Single, Allocator>>;

  explicit ObjectCache(size_t processor_count, ktl::unique_ptr<CpuCache[]> per_cpu_caches)
      : processor_count_{processor_count}, cpu_caches_{ktl::move(per_cpu_caches)} {}

  size_t processor_count_{0};
  ktl::unique_ptr<CpuCache[]> cpu_caches_;
};

}  // namespace object_cache

#endif  // ZIRCON_KERNEL_LIB_OBJECT_CACHE_INCLUDE_LIB_OBJECT_CACHE_H_
