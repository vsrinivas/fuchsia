// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/handle.h>

#include <object/dispatcher.h>
#include <fbl/arena.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/counters.h>
#include <pow2.h>

using fbl::AutoLock;

namespace {

// The number of possible handles in the arena.
constexpr size_t kMaxHandleCount = 256 * 1024u;

// Warning level: high_handle_count() is called when
// there are this many outstanding handles.
constexpr size_t kHighHandleCount = (kMaxHandleCount * 7) / 8;

KCOUNTER(handle_count_new, "kernel.handles.new");
KCOUNTER(handle_count_duped, "kernel.handles.duped");
KCOUNTER(handle_count_freed, "kernel.handles.freed");

// Masks for building a Handle's base_value, which ProcessDispatcher
// uses to create zx_handle_t values.
//
// base_value bit fields:
//   [31..30]: Must be zero
//   [29..kHandleGenerationShift]: Generation number
//                                 Masked by kHandleGenerationMask
//   [kHandleGenerationShift-1..0]: Index into handle_arena
//                                  Masked by kHandleIndexMask
constexpr uint32_t kHandleIndexMask = kMaxHandleCount - 1;
static_assert((kHandleIndexMask & kMaxHandleCount) == 0,
              "kMaxHandleCount must be a power of 2");
constexpr uint32_t kHandleGenerationMask = ~kHandleIndexMask & ~(3 << 30);
constexpr uint32_t kHandleGenerationShift = log2_uint_floor(kMaxHandleCount);
static_assert(((3 << (kHandleGenerationShift - 1)) & kHandleGenerationMask) ==
                  1 << kHandleGenerationShift,
              "Shift is wrong");
static_assert((kHandleGenerationMask >> kHandleGenerationShift) >= 255,
              "Not enough room for a useful generation count");
static_assert(((3 << 30) ^ kHandleGenerationMask ^ kHandleIndexMask) ==
                  0xffffffffu,
              "Masks do not agree");

}  // namespace

fbl::Mutex Handle::mutex_;
fbl::Arena Handle::arena_;

void Handle::Init() TA_NO_THREAD_SAFETY_ANALYSIS {
    arena_.Init("handles", sizeof(Handle), kMaxHandleCount);
}

// Returns a new |base_value| based on the value stored in the free
// arena slot pointed to by |addr|. The new value will be different
// from the last |base_value| used by this slot.
uint32_t Handle::GetNewBaseValue(void* addr) TA_REQ(mutex_) {
    // Get the index of this slot within the arena.
    uint32_t handle_index = HandleToIndex(reinterpret_cast<Handle*>(addr));
    DEBUG_ASSERT((handle_index & ~kHandleIndexMask) == 0);

    // Check the free memory for a stashed base_value.
    uint32_t v = *reinterpret_cast<uint32_t*>(addr);
    uint32_t old_gen = 0;
    if (v != 0) {
        // This slot has been used before.
        DEBUG_ASSERT((v & kHandleIndexMask) == handle_index);
        old_gen = (v & kHandleGenerationMask) >> kHandleGenerationShift;
    }
    uint32_t new_gen =
        (((old_gen + 1) << kHandleGenerationShift) & kHandleGenerationMask);
    return (handle_index | new_gen);
}

// Allocate space for a Handle from the arena, but don't instantiate the
// object.  |base_value| gets the value for Handle::base_value_.  |what|
// says whether this is allocation or duplication, for the error message.
void* Handle::Alloc(const fbl::RefPtr<Dispatcher>& dispatcher,
                    const char* what, uint32_t* base_value) {
    size_t outstanding_handles;
    {
        AutoLock lock(&mutex_);
        void* addr = arena_.Alloc();
        outstanding_handles = arena_.DiagnosticCount();
        if (likely(addr)) {
            if (outstanding_handles > kHighHandleCount) {
                // TODO: Avoid calling this for every handle after
                // kHighHandleCount; printfs are slow and we're
                // holding the mutex.
                printf("WARNING: High handle count: %zu handles\n",
                       outstanding_handles);
            }
            dispatcher->increment_handle_count();
            *base_value = GetNewBaseValue(addr);
            return addr;
        }
    }

    printf("WARNING: Could not allocate %s handle (%zu outstanding)\n",
           what, outstanding_handles);
    return nullptr;
}

HandleOwner Handle::Make(fbl::RefPtr<Dispatcher> dispatcher,
                         zx_rights_t rights) {
    uint32_t base_value;
    void* addr = Alloc(dispatcher, "new", &base_value);
    if (unlikely(!addr))
        return nullptr;
    kcounter_add(handle_count_new, 1u);
    return HandleOwner(new (addr) Handle(fbl::move(dispatcher),
                                         rights, base_value));
}

// Called only by Make.
Handle::Handle(fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights,
               uint32_t base_value)
    : process_id_(0u),
      dispatcher_(fbl::move(dispatcher)),
      rights_(rights),
      base_value_(base_value) {
}

HandleOwner Handle::Dup(Handle* source, zx_rights_t rights) {
    uint32_t base_value;
    void* addr = Alloc(source->dispatcher(), "duplicate", &base_value);
    if (unlikely(!addr))
        return nullptr;
    kcounter_add(handle_count_duped, 1u);
    return HandleOwner(new (addr) Handle(source, rights, base_value));
}

// Called only by Dup.
Handle::Handle(Handle* rhs, zx_rights_t rights, uint32_t base_value)
    : process_id_(rhs->process_id()),
      dispatcher_(rhs->dispatcher_),
      rights_(rights),
      base_value_(base_value) {
}

// Destroys, but does not free, the Handle, and fixes up its memory to protect
// against stale pointers to it. Also stashes the Handle's base_value for reuse
// the next time this slot is allocated.
void Handle::TearDown() TA_EXCL(mutex_) {
    uint32_t old_base_value = base_value();

    // Calling the handle dtor can cause many things to happen, so it is
    // important to call it outside the lock.
    this->~Handle();

    // There may be stale pointers to this slot. Zero out most of its fields
    // to ensure that the Handle does not appear to belong to any process
    // or point to any Dispatcher.
    memset(this, 0, sizeof(*this));

    // Hold onto the base_value for the next user of this slot, stashing
    // it at the beginning of the free slot.
    *reinterpret_cast<uint32_t*>(this) = old_base_value;

    // Double-check that the process_id field is zero, ensuring that
    // no process can refer to this slot while it's free. This isn't
    // completely legal since |handle| points to unconstructed memory,
    // but it should be safe enough for an assertion.
    DEBUG_ASSERT(process_id() == 0);
}

void Handle::Delete() {
    fbl::RefPtr<Dispatcher> disp = dispatcher();

    if (disp->has_state_tracker())
        disp->Cancel(this);

    TearDown();

    bool zero_handles = false;
    {
        AutoLock lock(&mutex_);
        zero_handles = disp->decrement_handle_count();
        arena_.Free(this);
    }

    if (zero_handles)
        disp->on_zero_handles();

    // If |disp| is the last reference then the dispatcher object
    // gets destroyed here.
    kcounter_add(handle_count_freed, 1u);
}

Handle* Handle::FromU32(uint32_t value) TA_NO_THREAD_SAFETY_ANALYSIS {
    Handle* handle = IndexToHandle(value & kHandleIndexMask);
    {
        AutoLock lock(&mutex_);
        if (unlikely(!arena_.in_range(handle)))
            return nullptr;
    }
    return likely(handle->base_value() == value) ? handle : nullptr;
}

uint32_t Handle::Count(const fbl::RefPtr<const Dispatcher>& dispatcher) {
    // Handle::mutex_ also guards Dispatcher::handle_count_.
    AutoLock lock(&mutex_);
    return dispatcher->current_handle_count();
}

size_t Handle::diagnostics::OutstandingHandles() {
    AutoLock lock(&mutex_);
    return arena_.DiagnosticCount();
}

void Handle::diagnostics::DumpTableInfo() {
    AutoLock lock(&mutex_);
    arena_.Dump();
}
