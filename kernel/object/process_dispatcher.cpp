// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/process_dispatcher.h>

#include <assert.h>
#include <inttypes.h>
#include <list.h>
#include <rand.h>
#include <string.h>
#include <trace.h>

#include <arch/defines.h>

#include <kernel/thread.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <lib/crypto/global_prng.h>
#include <lib/ktrace.h>

#include <zircon/rights.h>

#include <object/diagnostics.h>
#include <object/futex_context.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

static zx_handle_t map_handle_to_value(const Handle* handle, uint32_t mixer) {
    // Ensure that the last bit of the result is not zero, and make sure
    // we don't lose any base_value bits or make the result negative
    // when shifting.
    DEBUG_ASSERT((mixer & ((1<<31) | 0x1)) == 0);
    DEBUG_ASSERT((handle->base_value() & 0xc0000000) == 0);

    auto handle_id = (handle->base_value() << 1) | 0x1;
    return static_cast<zx_handle_t>(mixer ^ handle_id);
}

static Handle* map_value_to_handle(zx_handle_t value, uint32_t mixer) {
    auto handle_id = (static_cast<uint32_t>(value) ^ mixer) >> 1;
    return Handle::FromU32(handle_id);
}

zx_status_t ProcessDispatcher::Create(
    fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags,
    fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights,
    fbl::RefPtr<VmAddressRegionDispatcher>* root_vmar_disp,
    zx_rights_t* root_vmar_rights) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<ProcessDispatcher> process(new (&ac) ProcessDispatcher(job, name, flags));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    if (!job->AddChildProcess(process.get()))
        return ZX_ERR_BAD_STATE;

    zx_status_t result = process->Initialize();
    if (result != ZX_OK)
        return result;

    fbl::RefPtr<VmAddressRegion> vmar(process->aspace()->RootVmar());

    // Create a dispatcher for the root VMAR.
    fbl::RefPtr<Dispatcher> new_vmar_dispatcher;
    result = VmAddressRegionDispatcher::Create(vmar, &new_vmar_dispatcher, root_vmar_rights);
    if (result != ZX_OK) {
        process->aspace_->Destroy();
        return result;
    }

    *rights = ZX_DEFAULT_PROCESS_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(process.release());
    *root_vmar_disp = DownCastDispatcher<VmAddressRegionDispatcher>(
            &new_vmar_dispatcher);

    return ZX_OK;
}

ProcessDispatcher::ProcessDispatcher(fbl::RefPtr<JobDispatcher> job,
                                     fbl::StringPiece name,
                                     uint32_t flags)
  : job_(fbl::move(job)), policy_(job_->GetPolicy()),
    name_(name.data(), name.length()) {
    LTRACE_ENTRY_OBJ;

    // Generate handle XOR mask with top bit and bottom two bits cleared
    uint32_t secret;
    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->Draw(&secret, sizeof(secret));

    // Handle values cannot be negative values, so we mask the high bit.
    handle_rand_ = (secret << 2) & INT_MAX;
}

ProcessDispatcher::~ProcessDispatcher() {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT(state_ == State::INITIAL || state_ == State::DEAD);

    // Assert that the -> DEAD transition cleaned up what it should have.
    DEBUG_ASSERT(handles_.is_empty());
    DEBUG_ASSERT(exception_port_ == nullptr);
    DEBUG_ASSERT(debugger_exception_port_ == nullptr);

    // Remove ourselves from the parent job's raw ref to us. Note that this might
    // have beeen called when transitioning State::DEAD. The Job can handle double calls.
    job_->RemoveChildProcess(this);

    LTRACE_EXIT_OBJ;
}

void ProcessDispatcher::on_zero_handles() {
    // If the process is in the initial state and the last handle is closed
    // we never detach from the parent job, so run the shutdown sequence for
    // that case.
    {
        AutoLock lock(&state_lock_);
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

    AutoLock lock(&state_lock_);

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

void ProcessDispatcher::Exit(int retcode) {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT(ProcessDispatcher::GetCurrent() == this);

    {
        AutoLock lock(&state_lock_);

        // check that we're in the RUNNING state or we're racing with something
        // else that has already pushed us until the DYING state
        DEBUG_ASSERT_MSG(state_ == State::RUNNING || state_ == State::DYING,
                "state is %s", StateToString(state_));

        // Set the exit status if there isn't already an exit in progress.
        if (state_ != State::DYING) {
            DEBUG_ASSERT(retcode_ == 0);
            retcode_ = retcode;
        }

        // enter the dying state, which should kill all threads
        SetStateLocked(State::DYING);
    }

    ThreadDispatcher::GetCurrent()->Exit();

    __UNREACHABLE;
}

void ProcessDispatcher::Kill() {
    LTRACE_ENTRY_OBJ;

    // ZX-880: Call RemoveChildProcess outside of |state_lock_|.
    bool became_dead = false;

    {
        AutoLock lock(&state_lock_);

        // we're already dead
        if (state_ == State::DEAD)
            return;

        if (state_ != State::DYING) {
            // If there isn't an Exit already in progress, set a nonzero exit
            // status so e.g. crashing tests don't appear to have succeeded.
            DEBUG_ASSERT(retcode_ == 0);
            retcode_ = -1;
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

void ProcessDispatcher::KillAllThreadsLocked() {
    LTRACE_ENTRY_OBJ;

    for (auto& thread : thread_list_) {
        LTRACEF("killing thread %p\n", &thread);
        thread.Kill();
    }
}

zx_status_t ProcessDispatcher::AddThread(ThreadDispatcher* t, bool initial_thread) {
    LTRACE_ENTRY_OBJ;

    AutoLock state_lock(&state_lock_);

    if (initial_thread) {
        if (state_ != State::INITIAL)
            return ZX_ERR_BAD_STATE;
    } else {
        // We must not add a thread when in the DYING or DEAD states.
        // Also, we want to ensure that this is not the first thread.
        if (state_ != State::RUNNING)
            return ZX_ERR_BAD_STATE;
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

    // ZX-880: Call RemoveChildProcess outside of |state_lock_|.
    bool became_dead = false;

    {
        // we're going to check for state and possibly transition below
        AutoLock state_lock(&state_lock_);

        // remove the thread from our list
        DEBUG_ASSERT(t != nullptr);
        thread_list_.erase(*t);

        // if this was the last thread, transition directly to DEAD state
        if (thread_list_.is_empty()) {
            LTRACEF("last thread left the process %p, entering DEAD state\n", this);
            SetStateLocked(State::DEAD);
            became_dead = true;
        }
    }

    if (became_dead)
        FinishDeadTransition();
}

zx_koid_t ProcessDispatcher::get_related_koid() const {
    return job_->get_koid();
}

ProcessDispatcher::State ProcessDispatcher::state() const {
    AutoLock lock(&state_lock_);
    return state_;
}

fbl::RefPtr<JobDispatcher> ProcessDispatcher::job() {
    return job_;
}

void ProcessDispatcher::SetStateLocked(State s) {
    LTRACEF("process %p: state %u (%s)\n", this, static_cast<unsigned int>(s), StateToString(s));

    DEBUG_ASSERT(state_lock_.IsHeld());

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
// outside of holding |state_lock_|. Beware this is called from several places
// including on_zero_handles().
void ProcessDispatcher::FinishDeadTransition() {
    DEBUG_ASSERT(!completely_dead_);
    completely_dead_ = true;

    // clean up the handle table
    LTRACEF_LEVEL(2, "cleaning up handle table on proc %p\n", this);

    fbl::DoublyLinkedList<Handle*> to_clean;
    {
        AutoLock lock(&handle_table_lock_);
        for (auto& handle : handles_) {
            handle.set_process_id(0u);
        }
        to_clean.swap(handles_);
    }

    // zx-1544: Here is where if we're the last holder of a handle of one of
    // our exception ports then ResetExceptionPort will get called (by
    // ExceptionPort::OnPortZeroHandles) and will need to grab |state_lock_|.
    // This needs to be done outside of |state_lock_|.
    while (!to_clean.is_empty()) {
        // Delete handle via HandleOwner dtor.
        HandleOwner ho(to_clean.pop_front());
    }

    LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

    // tear down the address space
    aspace_->Destroy();

    // signal waiter
    LTRACEF_LEVEL(2, "signaling waiters\n");
    UpdateState(0u, ZX_TASK_TERMINATED);

    // The PROC_CREATE record currently emits a uint32_t koid.
    uint32_t koid = static_cast<uint32_t>(get_koid());
    ktrace(TAG_PROC_EXIT, koid, 0, 0, 0);

    // Call job_->RemoveChildProcess(this) outside of |state_lock_|. Otherwise
    // we risk a deadlock as we have |state_lock_| and RemoveChildProcess grabs
    // the job's |lock_|, whereas JobDispatcher::EnumerateChildren obtains the
    // locks in the opposite order. We want to keep lock acquisition order
    // consistent, and JobDispatcher::EnumerateChildren's order makes
    // sense. We don't need |state_lock_| when calling RemoveChildProcess
    // here. ZX-880
    // RemoveChildProcess is called soon after releasing |state_lock_| so that
    // the semantics of signaling ZX_JOB_NO_PROCESSES match that of
    // ZX_TASK_TERMINATED.
    job_->RemoveChildProcess(this);
}

// process handle manipulation routines
zx_handle_t ProcessDispatcher::MapHandleToValue(const Handle* handle) const {
    return map_handle_to_value(handle, handle_rand_);
}

zx_handle_t ProcessDispatcher::MapHandleToValue(const HandleOwner& handle) const {
    return map_handle_to_value(handle.get(), handle_rand_);
}

Handle* ProcessDispatcher::GetHandleLocked(zx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    if (handle && handle->process_id() == get_koid())
        return handle;

    // Handle lookup failed.  We potentially generate an exception,
    // depending on the job policy.  Note that we don't use the return
    // value from QueryPolicy() here: ZX_POL_ACTION_ALLOW and
    // ZX_POL_ACTION_DENY are equivalent for ZX_POL_BAD_HANDLE.
    QueryPolicy(ZX_POL_BAD_HANDLE);
    return nullptr;
}

void ProcessDispatcher::AddHandle(HandleOwner handle) {
    AutoLock lock(&handle_table_lock_);
    AddHandleLocked(fbl::move(handle));
}

void ProcessDispatcher::AddHandleLocked(HandleOwner handle) {
    handle->set_process_id(get_koid());
    handles_.push_front(handle.release());
}

HandleOwner ProcessDispatcher::RemoveHandle(zx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return RemoveHandleLocked(handle_value);
}

HandleOwner ProcessDispatcher::RemoveHandleLocked(zx_handle_t handle_value) {
    auto handle = GetHandleLocked(handle_value);
    if (!handle)
        return nullptr;

    handle->set_process_id(0u);
    handles_.erase(*handle);

    return HandleOwner(handle);
}

void ProcessDispatcher::UndoRemoveHandleLocked(zx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    AddHandleLocked(HandleOwner(handle));
}

zx_koid_t ProcessDispatcher::GetKoidForHandle(zx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return ZX_KOID_INVALID;
    return handle->dispatcher()->get_koid();
}

zx_status_t ProcessDispatcher::GetDispatcherInternal(zx_handle_t handle_value,
                                                     fbl::RefPtr<Dispatcher>* dispatcher,
                                                     zx_rights_t* rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return ZX_ERR_BAD_HANDLE;

    *dispatcher = handle->dispatcher();
    if (rights)
        *rights = handle->rights();
    return ZX_OK;
}

zx_status_t ProcessDispatcher::GetDispatcherWithRightsInternal(zx_handle_t handle_value,
                                                               zx_rights_t desired_rights,
                                                               fbl::RefPtr<Dispatcher>* dispatcher_out,
                                                               zx_rights_t* out_rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return ZX_ERR_BAD_HANDLE;

    if (!handle->HasRights(desired_rights))
        return ZX_ERR_ACCESS_DENIED;

    *dispatcher_out = handle->dispatcher();
    if (out_rights)
        *out_rights = handle->rights();
    return ZX_OK;
}

zx_status_t ProcessDispatcher::GetInfo(zx_info_process_t* info) {
    memset(info, 0, sizeof(*info));

    // retcode_ depends on the state: make sure they're consistent.
    state_lock_.Acquire();
    int retcode = retcode_;
    State state = state_;
    if (debugger_exception_port_) {  // TODO: Protect with rights if necessary.
        info->debugger_attached = true;
    }
    state_lock_.Release();

    switch (state) {
    case State::DEAD:
    case State::DYING:
        info->return_code = retcode;
        info->exited = true;
        /* fallthrough */
    case State::RUNNING:
        info->started = true;
        break;
    case State::INITIAL:
    default:
        break;
    }

    return ZX_OK;
}

zx_status_t ProcessDispatcher::GetStats(zx_info_task_stats_t* stats) {
    DEBUG_ASSERT(stats != nullptr);
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
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

zx_status_t ProcessDispatcher::GetAspaceMaps(
    user_out_ptr<zx_info_maps_t> maps, size_t max,
    size_t* actual, size_t* available) {
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
        return ZX_ERR_BAD_STATE;
    }
    return GetVmAspaceMaps(aspace_, maps, max, actual, available);
}

zx_status_t ProcessDispatcher::GetVmos(
    user_out_ptr<zx_info_vmo_t> vmos, size_t max,
    size_t* actual_out, size_t* available_out) {
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
        return ZX_ERR_BAD_STATE;
    }
    size_t actual = 0;
    size_t available = 0;
    zx_status_t s = GetProcessVmosViaHandles(this, vmos, max, &actual, &available);
    if (s != ZX_OK) {
        return s;
    }
    size_t actual2 = 0;
    size_t available2 = 0;
    DEBUG_ASSERT(max >= actual);
    s = GetVmAspaceVmos(aspace_, vmos.element_offset(actual), max - actual,
                        &actual2, &available2);
    if (s != ZX_OK) {
        return s;
    }
    *actual_out = actual + actual2;
    *available_out = available + available2;
    return ZX_OK;
}

zx_status_t ProcessDispatcher::GetThreads(fbl::Array<zx_koid_t>* out_threads) {
    AutoLock lock(&state_lock_);
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
    *out_threads = fbl::move(threads);
    return ZX_OK;
}

zx_status_t ProcessDispatcher::SetExceptionPort(fbl::RefPtr<ExceptionPort> eport) {
    LTRACE_ENTRY_OBJ;
    bool debugger = false;
    switch (eport->type()) {
    case ExceptionPort::Type::DEBUGGER:
        debugger = true;
        break;
    case ExceptionPort::Type::PROCESS:
        break;
    default:
        DEBUG_ASSERT_MSG(false, "unexpected port type: %d",
                         static_cast<int>(eport->type()));
        break;
    }

    // Lock |state_lock_| to ensure the process doesn't transition to dead
    // while we're setting the exception handler.
    AutoLock state_lock(&state_lock_);
    if (state_ == State::DEAD)
        return ZX_ERR_NOT_FOUND;
    if (debugger) {
        if (debugger_exception_port_)
            return ZX_ERR_BAD_STATE;
        debugger_exception_port_ = eport;
    } else {
        if (exception_port_)
            return ZX_ERR_BAD_STATE;
        exception_port_ = eport;
    }

    return ZX_OK;
}

bool ProcessDispatcher::ResetExceptionPort(bool debugger, bool quietly) {
    LTRACE_ENTRY_OBJ;
    fbl::RefPtr<ExceptionPort> eport;

    // Remove the exception handler first. As we resume threads we don't
    // want them to hit another exception and get back into
    // ExceptionHandlerExchange.
    {
        AutoLock lock(&state_lock_);
        if (debugger) {
            debugger_exception_port_.swap(eport);
        } else {
            exception_port_.swap(eport);
        }
        if (eport == nullptr) {
            // Attempted to unbind when no exception port is bound.
            return false;
        }
        // This method must guarantee that no caller will return until
        // OnTargetUnbind has been called on the port-to-unbind.
        // This becomes important when a manual unbind races with a
        // PortDispatcher::on_zero_handles auto-unbind.
        //
        // If OnTargetUnbind were called outside of the lock, it would lead to
        // a race (for threads A and B):
        //
        //   A: Calls ResetExceptionPort; acquires the lock
        //   A: Sees a non-null exception_port_, swaps it into the eport local.
        //      exception_port_ is now null.
        //   A: Releases the lock
        //
        //   B: Calls ResetExceptionPort; acquires the lock
        //   B: Sees a null exception_port_ and returns. But OnTargetUnbind()
        //      hasn't yet been called for the port.
        //
        // So, call it before releasing the lock.
        eport->OnTargetUnbind();
    }

    if (!quietly) {
        OnExceptionPortRemoval(eport);
    }
    return true;
}

fbl::RefPtr<ExceptionPort> ProcessDispatcher::exception_port() {
    AutoLock lock(&state_lock_);
    return exception_port_;
}

fbl::RefPtr<ExceptionPort> ProcessDispatcher::debugger_exception_port() {
    AutoLock lock(&state_lock_);
    return debugger_exception_port_;
}

void ProcessDispatcher::OnExceptionPortRemoval(
        const fbl::RefPtr<ExceptionPort>& eport) {
    AutoLock lock(&state_lock_);
    for (auto& thread : thread_list_) {
        thread.OnExceptionPortRemoval(eport);
    }
}

uint32_t ProcessDispatcher::ThreadCount() const {
    canary_.Assert();

    fbl::AutoLock lock(&state_lock_);
    return static_cast<uint32_t>(thread_list_.size_slow());
}

size_t ProcessDispatcher::PageCount() const {
    canary_.Assert();

    AutoLock lock(&state_lock_);
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
            pd_ = fbl::WrapRefPtr(process);
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
    AutoLock lock(&state_lock_);

    auto iter = thread_list_.find_if([koid](const ThreadDispatcher& t) { return t.get_koid() == koid; });
    return fbl::WrapRefPtr(iter.CopyPointer());
}

uintptr_t ProcessDispatcher::get_debug_addr() const {
    AutoLock lock(&state_lock_);
    return debug_addr_;
}

zx_status_t ProcessDispatcher::set_debug_addr(uintptr_t addr) {
    if (addr == 0u)
        return ZX_ERR_INVALID_ARGS;
    AutoLock lock(&state_lock_);
    // Only allow the value to be set once: Once ld.so has set it that's it.
    if (debug_addr_ != 0u)
        return ZX_ERR_ACCESS_DENIED;
    debug_addr_ = addr;
    return ZX_OK;
}

zx_status_t ProcessDispatcher::QueryPolicy(uint32_t condition) const {
    auto action = GetSystemPolicyManager()->QueryBasicPolicy(policy_, condition);
    if (action & ZX_POL_ACTION_EXCEPTION) {
        thread_signal_policy_exception();
    }
    // TODO(cpu): check for the ZX_POL_KILL bit and return an error code
    // that abigen understands as termination.
    return (action & ZX_POL_ACTION_DENY) ? ZX_ERR_ACCESS_DENIED : ZX_OK;
}

uintptr_t ProcessDispatcher::cache_vdso_code_address() {
    AutoLock a(&state_lock_);
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
    AutoLock lock(&handle_table_lock_);
    return (GetHandleLocked(handle_value) != nullptr);
}
