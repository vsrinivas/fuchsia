// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/magenta.h>

#include <pow2.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/cmdline.h>
#include <kernel/mutex.h>

#include <lk/init.h>

#include <lib/console.h>

#include <magenta/dispatcher.h>
#include <magenta/excp_port.h>
#include <magenta/job_dispatcher.h>
#include <magenta/handle.h>
#include <magenta/policy_manager.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <magenta/state_tracker.h>

// The next two includes should be removed. See DeleteHandle().
#include <magenta/io_mapping_dispatcher.h>

#include <mxtl/arena.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/type_support.h>

#include <platform.h>

#define LOCAL_TRACE 0

// The number of possible handles in the arena.
constexpr size_t kMaxHandleCount = 256 * 1024u;

// Warning level: high_handle_count() is called when
// there are this many outstanding handles.
constexpr size_t kHighHandleCount = (kMaxHandleCount * 7) / 8;

// The handle arena and its mutex. It also guards Dispatcher::handle_count_.
static Mutex handle_mutex;
static mxtl::Arena TA_GUARDED(handle_mutex) handle_arena;
static size_t outstanding_handles TA_GUARDED(handle_mutex) = 0u;

size_t internal::OutstandingHandles() {
    AutoLock lock(&handle_mutex);
    return outstanding_handles;
}

// The system exception port.
static mutex_t system_exception_mutex = MUTEX_INITIAL_VALUE(system_exception_mutex);
static mxtl::RefPtr<ExceptionPort> system_exception_port TA_GUARDED(system_exception_mutex);

// All jobs and processes are rooted at the |root_job|.
static mxtl::RefPtr<JobDispatcher> root_job;

// The singleton policy manager, for jobs and processes. This is
// a magenta internal class (not a dispatcher-derived).
static PolicyManager* policy_manager;

void magenta_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
    handle_arena.Init("handles", sizeof(Handle), kMaxHandleCount);
    root_job = JobDispatcher::CreateRootJob();
    policy_manager = PolicyManager::Create();
}

// Masks for building a Handle's base_value, which ProcessDispatcher
// uses to create mx_handle_t values.
//
// base_value bit fields:
//   [31..30]: Must be zero
//   [29..kHandleGenerationShift]: Generation number
//                                 Masked by kHandleGenerationMask
//   [kHandleGenerationShift-1..0]: Index into handle_arena
//                                  Masked by kHandleIndexMask
static constexpr uint32_t kHandleIndexMask = kMaxHandleCount - 1;
static_assert((kHandleIndexMask & kMaxHandleCount) == 0,
              "kMaxHandleCount must be a power of 2");
static constexpr uint32_t kHandleGenerationMask =
    ~kHandleIndexMask & ~(3 << 30);
static constexpr uint32_t kHandleGenerationShift =
    log2_uint_floor(kMaxHandleCount);
static_assert(((3 << (kHandleGenerationShift - 1)) & kHandleGenerationMask) ==
                  1 << kHandleGenerationShift,
              "Shift is wrong");
static_assert((kHandleGenerationMask >> kHandleGenerationShift) >= 255,
              "Not enough room for a useful generation count");
static_assert(((3 << 30) ^ kHandleGenerationMask ^ kHandleIndexMask) ==
                  0xffffffffu,
              "Masks do not agree");

// Returns a new |base_value| based on the value stored in the free
// |handle_arena| slot pointed to by |addr|. The new value will be different
// from the last |base_value| used by this slot.
static uint32_t GetNewHandleBaseValue(void* addr) TA_REQ(handle_mutex) {
    // Get the index of this slot within handle_arena.
    auto va = reinterpret_cast<Handle*>(addr) -
              reinterpret_cast<Handle*>(handle_arena.start());
    uint32_t handle_index = static_cast<uint32_t>(va);
    DEBUG_ASSERT((handle_index & ~kHandleIndexMask) == 0);

    // Check the free memory for a stashed base_value.
    uint32_t v = *reinterpret_cast<uint32_t*>(addr);
    uint32_t old_gen;
    if (v == 0) {
        // First time this slot has been allocated.
        old_gen = 0;
    } else {
        // This slot has been used before.
        DEBUG_ASSERT((v & kHandleIndexMask) == handle_index);
        old_gen = (v & kHandleGenerationMask) >> kHandleGenerationShift;
    }
    return (((old_gen + 1) << kHandleGenerationShift) & kHandleGenerationMask) | handle_index;
}

// Destroys, but does not free, the Handle, and fixes up its memory to protect
// against stale pointers to it. Also stashes the Handle's base_value for reuse
// the next time this slot is allocated.
void internal::TearDownHandle(Handle *handle) TA_EXCL(handle_mutex) {
    uint32_t base_value = handle->base_value();

    // Calling the handle dtor can cause many things to happen, so it is
    // important to call it outside the lock.
    handle->~Handle();

    // There may be stale pointers to this slot. Zero out most of its fields
    // to ensure that the Handle does not appear to belong to any process
    // or point to any Dispatcher.
    memset(handle, 0, sizeof(Handle));

    // Hold onto the base_value for the next user of this slot, stashing
    // it at the beginning of the free slot.
    *reinterpret_cast<uint32_t*>(handle) = base_value;

    // Double-check that the process_id field is zero, ensuring that
    // no process can refer to this slot while it's free. This isn't
    // completely legal since |handle| points to unconstructed memory,
    // but it should be safe enough for an assertion.
    DEBUG_ASSERT(handle->process_id_ == 0);
}

static void high_handle_count(size_t count) {
    // TODO: Avoid calling this for every handle after kHighHandleCount;
    // printfs are slow and |handle_mutex| is held by our caller.
    printf("WARNING: High handle count: %zu handles\n", count);
}

Handle* MakeHandle(mxtl::RefPtr<Dispatcher> dispatcher, mx_rights_t rights) {
    uint32_t* handle_count = nullptr;
    void* addr;
    uint32_t base_value;

    {
        AutoLock lock(&handle_mutex);
        addr = handle_arena.Alloc();
        if (addr == nullptr) {
            const auto oh = outstanding_handles;
            lock.release();
            printf("WARNING: Could not allocate new handle (%zu outstanding)\n",
                   oh);
            return nullptr;
        }
        if (++outstanding_handles > kHighHandleCount)
            high_handle_count(outstanding_handles);

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)++;
        if (*handle_count != 2u)
            handle_count = nullptr;

        base_value = GetNewHandleBaseValue(addr);
    }

    auto state_tracker = dispatcher->get_state_tracker();
    if (state_tracker != nullptr)
        state_tracker->UpdateLastHandleSignal(handle_count);

    return new (addr) Handle(mxtl::move(dispatcher), rights, base_value);
}

Handle* DupHandle(Handle* source, mx_rights_t rights, bool is_replace) {
    mxtl::RefPtr<Dispatcher> dispatcher(source->dispatcher());
    uint32_t* handle_count;
    void* addr;
    uint32_t base_value;

    {
        AutoLock lock(&handle_mutex);
        addr = handle_arena.Alloc();
        if (addr == nullptr) {
            const auto oh = outstanding_handles;
            lock.release();
            printf(
                "WARNING: Could not allocate duplicate handle (%zu outstanding)\n", oh);
            return nullptr;
        }
        if (++outstanding_handles > kHighHandleCount)
            high_handle_count(outstanding_handles);

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)++;
        if (*handle_count != 2u)
            handle_count = nullptr;

        base_value = GetNewHandleBaseValue(addr);
    }

    auto state_tracker = dispatcher->get_state_tracker();
    if (!is_replace && (state_tracker != nullptr))
        state_tracker->UpdateLastHandleSignal(handle_count);

    return new (addr) Handle(source, rights, base_value);
}

void DeleteHandle(Handle* handle) {
    mxtl::RefPtr<Dispatcher> dispatcher(handle->dispatcher());
    auto state_tracker = dispatcher->get_state_tracker();

    if (state_tracker) {
        state_tracker->Cancel(handle);
    } else {
        // This code is sad but necessary because certain dispatchers
        // have complicated Close() logic which cannot be untangled at
        // this time.
        switch (dispatcher->get_type()) {
            case MX_OBJ_TYPE_IOMAP: {
                // DownCastDispatcher moves the reference so we need a copy
                // because we use |dispatcher| after the cast.
                auto disp = dispatcher;
                auto iodisp = DownCastDispatcher<IoMappingDispatcher>(&disp);
                if (iodisp)
                    iodisp->Close();
                break;
            }
            default:  break;
                // This is fine. See for example the LogDispatcher.
        };
    }

    // Destroys, but does not free, the Handle, and fixes up its memory
    // to protect against stale pointers to it. Also stashes the Handle's
    // base_value for reuse the next time this slot is allocated.
    internal::TearDownHandle(handle);

    bool zero_handles = false;
    uint32_t* handle_count;
    {
        AutoLock lock(&handle_mutex);
        --outstanding_handles;

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)--;
        if (*handle_count == 0u)
            zero_handles = true;
        else if (*handle_count != 1u)
            handle_count = nullptr;

        handle_arena.Free(handle);
    }

    if (zero_handles) {
        dispatcher->on_zero_handles();
        return;
    }

    if (state_tracker)
        state_tracker->UpdateLastHandleSignal(handle_count);

    // If |dispatcher| is the last reference then the dispatcher object
    // gets destroyed here.
}

bool HandleInRange(void* addr) {
    AutoLock lock(&handle_mutex);
    return handle_arena.in_range(addr);
}

Handle* MapU32ToHandle(uint32_t value) TA_NO_THREAD_SAFETY_ANALYSIS {
    auto index = value & kHandleIndexMask;
    auto va = &reinterpret_cast<Handle*>(handle_arena.start())[index];
    if (!HandleInRange(va))
        return nullptr;
    Handle *handle = reinterpret_cast<Handle*>(va);
    return handle->base_value() == value ? handle : nullptr;
}

void internal::DumpHandleTableInfo() {
    AutoLock lock(&handle_mutex);
    handle_arena.Dump();
}

mx_status_t SetSystemExceptionPort(mxtl::RefPtr<ExceptionPort> eport) {
    DEBUG_ASSERT(eport->type() == ExceptionPort::Type::SYSTEM);

    AutoLock lock(&system_exception_mutex);
    if (system_exception_port)
        return ERR_BAD_STATE; // TODO(dje): ?
    system_exception_port = eport;
    return NO_ERROR;
}

bool ResetSystemExceptionPort() {
    AutoLock lock(&system_exception_mutex);
    if (system_exception_port == nullptr) {
        // Attempted to unbind when no exception port is bound.
        return false;
    }
    system_exception_port->OnTargetUnbind();
    system_exception_port.reset();
    return true;
}

mxtl::RefPtr<ExceptionPort> GetSystemExceptionPort() {
    AutoLock lock(&system_exception_mutex);
    return system_exception_port;
}

mxtl::RefPtr<JobDispatcher> GetRootJobDispatcher() {
    return root_job;
}

PolicyManager* GetSystemPolicyManager() {
    return policy_manager;
}

bool magenta_rights_check(const Handle* handle, mx_rights_t desired) {
    auto actual = handle->rights();
    if ((actual & desired) == desired)
        return true;
    LTRACEF("rights check fail!! has 0x%x, needs 0x%x\n", actual, desired);
    return false;
}

mx_status_t magenta_sleep(mx_time_t deadline) {
    /* sleep with interruptable flag set */
    return thread_sleep_etc(deadline, true);
}

mx_status_t validate_resource_handle(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<ResourceDispatcher> resource;
    return up->GetDispatcher(handle, &resource);
}

mx_status_t get_process(ProcessDispatcher* up,
                        mx_handle_t proc_handle,
                        mxtl::RefPtr<ProcessDispatcher>* proc) {
    return up->GetDispatcherWithRights(proc_handle, MX_RIGHT_WRITE, proc);
}


LK_INIT_HOOK(magenta, magenta_init, LK_INIT_LEVEL_THREADING);
