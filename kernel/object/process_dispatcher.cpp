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
#include <kernel/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <lib/crypto/global_prng.h>
#include <lib/ktrace.h>

#include <magenta/rights.h>

#include <object/diagnostics.h>
#include <object/futex_context.h>
#include <object/handle_owner.h>
#include <object/handle_reaper.h>
#include <object/handles.h>
#include <object/job_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

static mx_handle_t map_handle_to_value(const Handle* handle, mx_handle_t mixer) {
    // Ensure that the last bit of the result is not zero, and make sure
    // we don't lose any base_value bits or make the result negative
    // when shifting.
    DEBUG_ASSERT((mixer & ((1<<31) | 0x1)) == 0);
    DEBUG_ASSERT((handle->base_value() & 0xc0000000) == 0);

    auto handle_id = (handle->base_value() << 1) | 0x1;
    return mixer ^ handle_id;
}

static Handle* map_value_to_handle(mx_handle_t value, mx_handle_t mixer) {
    auto handle_id = (value ^ mixer) >> 1;
    return MapU32ToHandle(handle_id);
}

mx_status_t ProcessDispatcher::Create(
    fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags,
    fbl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights,
    fbl::RefPtr<VmAddressRegionDispatcher>* root_vmar_disp,
    mx_rights_t* root_vmar_rights) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<ProcessDispatcher> process(new (&ac) ProcessDispatcher(job, name, flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    if (!job->AddChildProcess(process.get()))
        return MX_ERR_BAD_STATE;

    mx_status_t result = process->Initialize();
    if (result != MX_OK)
        return result;

    fbl::RefPtr<VmAddressRegion> vmar(process->aspace()->RootVmar());

    // Create a dispatcher for the root VMAR.
    fbl::RefPtr<Dispatcher> new_vmar_dispatcher;
    result = VmAddressRegionDispatcher::Create(vmar, &new_vmar_dispatcher, root_vmar_rights);
    if (result != MX_OK) {
        process->aspace_->Destroy();
        return result;
    }

    *rights = MX_DEFAULT_PROCESS_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(process.release());
    *root_vmar_disp = DownCastDispatcher<VmAddressRegionDispatcher>(
            &new_vmar_dispatcher);

    return MX_OK;
}

ProcessDispatcher::ProcessDispatcher(fbl::RefPtr<JobDispatcher> job,
                                     fbl::StringPiece name,
                                     uint32_t flags)
  : job_(fbl::move(job)), policy_(job_->GetPolicy()), state_tracker_(0u),
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

void ProcessDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    name_.get(MX_MAX_NAME_LEN, out_name);
}

mx_status_t ProcessDispatcher::set_name(const char* name, size_t len) {
    return name_.set(name, len);
}

mx_status_t ProcessDispatcher::Initialize() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // create an address space for this process, named after the process's koid.
    char aspace_name[MX_MAX_NAME_LEN];
    snprintf(aspace_name, sizeof(aspace_name), "proc:%" PRIu64, get_koid());
    aspace_ = VmAspace::Create(VmAspace::TYPE_USER, aspace_name);
    if (!aspace_) {
        TRACEF("error creating address space\n");
        return MX_ERR_NO_MEMORY;
    }

    return MX_OK;
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

    // MG-880: Call RemoveChildProcess outside of |state_lock_|.
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
        job_->RemoveChildProcess(this);
}

void ProcessDispatcher::KillAllThreadsLocked() {
    LTRACE_ENTRY_OBJ;

    for (auto& thread : thread_list_) {
        LTRACEF("killing thread %p\n", &thread);
        thread.Kill();
    }
}

mx_status_t ProcessDispatcher::AddThread(ThreadDispatcher* t, bool initial_thread) {
    LTRACE_ENTRY_OBJ;

    AutoLock state_lock(&state_lock_);

    if (initial_thread) {
        if (state_ != State::INITIAL)
            return MX_ERR_BAD_STATE;
    } else {
        // We must not add a thread when in the DYING or DEAD states.
        // Also, we want to ensure that this is not the first thread.
        if (state_ != State::RUNNING)
            return MX_ERR_BAD_STATE;
    }

    // add the thread to our list
    DEBUG_ASSERT(thread_list_.is_empty() == initial_thread);
    thread_list_.push_back(t);

    DEBUG_ASSERT(t->process() == this);

    if (initial_thread)
        SetStateLocked(State::RUNNING);

    return MX_OK;
}

// This is called within thread T's context when it is exiting.

void ProcessDispatcher::RemoveThread(ThreadDispatcher* t) {
    LTRACE_ENTRY_OBJ;

    // MG-880: Call RemoveChildProcess outside of |state_lock_|.
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
        job_->RemoveChildProcess(this);
}

void ProcessDispatcher::on_zero_handles() {
    LTRACE_ENTRY_OBJ;

    // last handle going away acts as a kill to the process object
    Kill();
}

mx_koid_t ProcessDispatcher::get_related_koid() const {
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
    } else if (s == State::DEAD) {
        // clean up the handle table
        LTRACEF_LEVEL(2, "cleaning up handle table on proc %p\n", this);
        {
            AutoLock lock(&handle_table_lock_);
            for (auto& handle : handles_) {
                handle.set_process_id(0u);
            }
            // Delete handles out-of-band to avoid the worst case recursive
            // destruction behavior.
            ReapHandles(&handles_);
        }
        LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

        // tear down the address space
        aspace_->Destroy();

        // Send out exception reports before signalling MX_TASK_TERMINATED,
        // the theory being that marking the process as terminated is the
        // last thing that is done.
        //
        // Note: If we need OnProcessExit for the debugger to do an exchange
        // with the debugger then this should preceed aspace destruction.
        // For now it is left here, following aspace destruction.
        //
        // Note: If an eport is bound, it will have a reference to the
        // ProcessDispatcher and thus keep the object around until someone
        // unbinds the port or closes all handles to its underlying
        // PortDispatcher.
        //
        // There's no need to hold |exception_lock_| across OnProcessExit
        // here so don't. We don't assume anything about what OnProcessExit
        // does. If it blocks the exception port could get removed out from
        // underneath us, so make a copy.
        {
            fbl::RefPtr<ExceptionPort> eport(exception_port());
            if (eport) {
                eport->OnProcessExit(this);
            }
        }
        {
            fbl::RefPtr<ExceptionPort> debugger_eport(debugger_exception_port());
            if (debugger_eport) {
                debugger_eport->OnProcessExit(this);
            }
        }

        // signal waiter
        LTRACEF_LEVEL(2, "signaling waiters\n");
        state_tracker_.UpdateState(0u, MX_TASK_TERMINATED);

        // IWBN to call job_->RemoveChildProcess(this) here, but that risks
        // a deadlock as we have |state_lock_| and RemoveChildProcess grabs the
        // job's |lock_|, whereas JobDispatcher::EnumerateChildren obtains the
        // locks in the opposite order. We want to keep lock acquisition order
        // consistent, and JobDispatcher::EnumerateChildren's order makes
        // sense. We don't need |state_lock_| when calling RemoveChildProcess
        // here, so we leave that to the caller after it has released
        // |state_lock_|. MG-880
        // The caller should call RemoveChildProcess soon so that the semantics
        // of signaling MX_JOB_NO_PROCESSES match that of MX_TASK_TERMINATED.

        // The PROC_CREATE record currently emits a uint32_t.
        uint32_t koid = static_cast<uint32_t>(get_koid());
        ktrace(TAG_PROC_EXIT, koid, 0, 0, 0);
    }
}

// process handle manipulation routines
mx_handle_t ProcessDispatcher::MapHandleToValue(const Handle* handle) const {
    return map_handle_to_value(handle, handle_rand_);
}

mx_handle_t ProcessDispatcher::MapHandleToValue(const HandleOwner& handle) const {
    return map_handle_to_value(handle.get(), handle_rand_);
}

Handle* ProcessDispatcher::GetHandleLocked(mx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    if (handle && handle->process_id() == get_koid())
        return handle;

    // Handle lookup failed.  We potentially generate an exception,
    // depending on the job policy.  Note that we don't use the return
    // value from QueryPolicy() here: MX_POL_ACTION_ALLOW and
    // MX_POL_ACTION_DENY are equivalent for MX_POL_BAD_HANDLE.
    QueryPolicy(MX_POL_BAD_HANDLE);
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

HandleOwner ProcessDispatcher::RemoveHandle(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return RemoveHandleLocked(handle_value);
}

HandleOwner ProcessDispatcher::RemoveHandleLocked(mx_handle_t handle_value) {
    auto handle = GetHandleLocked(handle_value);
    if (!handle)
        return nullptr;

    handle->set_process_id(0u);
    handles_.erase(*handle);

    return HandleOwner(handle);
}

void ProcessDispatcher::UndoRemoveHandleLocked(mx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    AddHandleLocked(HandleOwner(handle));
}

mx_koid_t ProcessDispatcher::GetKoidForHandle(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return MX_KOID_INVALID;
    return handle->dispatcher()->get_koid();
}

mx_status_t ProcessDispatcher::GetDispatcherInternal(mx_handle_t handle_value,
                                                     fbl::RefPtr<Dispatcher>* dispatcher,
                                                     mx_rights_t* rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return MX_ERR_BAD_HANDLE;

    *dispatcher = handle->dispatcher();
    if (rights)
        *rights = handle->rights();
    return MX_OK;
}

mx_status_t ProcessDispatcher::GetDispatcherWithRightsInternal(mx_handle_t handle_value,
                                                               mx_rights_t desired_rights,
                                                               fbl::RefPtr<Dispatcher>* dispatcher_out,
                                                               mx_rights_t* out_rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandleLocked(handle_value);
    if (!handle)
        return MX_ERR_BAD_HANDLE;

    if (!handle->HasRights(desired_rights))
        return MX_ERR_ACCESS_DENIED;

    *dispatcher_out = handle->dispatcher();
    if (out_rights)
        *out_rights = handle->rights();
    return MX_OK;
}

mx_status_t ProcessDispatcher::GetInfo(mx_info_process_t* info) {
    // retcode_ depends on the state: make sure they're consistent.
    state_lock_.Acquire();
    int retcode = retcode_;
    State state = state_;
    state_lock_.Release();

    memset(info, 0, sizeof(*info));
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
    {
        AutoLock lock(&exception_lock_);
        if (debugger_exception_port_) {  // TODO: Protect with rights if necessary.
            info->debugger_attached = true;
        }
    }
    return MX_OK;
}

mx_status_t ProcessDispatcher::GetStats(mx_info_task_stats_t* stats) {
    DEBUG_ASSERT(stats != nullptr);
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
        return MX_ERR_BAD_STATE;
    }
    VmAspace::vm_usage_t usage;
    mx_status_t s = aspace_->GetMemoryUsage(&usage);
    if (s != MX_OK) {
        return s;
    }
    stats->mem_mapped_bytes = usage.mapped_pages * PAGE_SIZE;
    stats->mem_private_bytes = usage.private_pages * PAGE_SIZE;
    stats->mem_shared_bytes = usage.shared_pages * PAGE_SIZE;
    stats->mem_scaled_shared_bytes = usage.scaled_shared_bytes;
    return MX_OK;
}

mx_status_t ProcessDispatcher::GetAspaceMaps(
    user_ptr<mx_info_maps_t> maps, size_t max,
    size_t* actual, size_t* available) {
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
        return MX_ERR_BAD_STATE;
    }
    return GetVmAspaceMaps(aspace_, maps, max, actual, available);
}

mx_status_t ProcessDispatcher::GetVmos(
    user_ptr<mx_info_vmo_t> vmos, size_t max,
    size_t* actual_out, size_t* available_out) {
    AutoLock lock(&state_lock_);
    if (state_ != State::RUNNING) {
        return MX_ERR_BAD_STATE;
    }
    size_t actual = 0;
    size_t available = 0;
    mx_status_t s = GetProcessVmosViaHandles(this, vmos, max, &actual, &available);
    if (s != MX_OK) {
        return s;
    }
    size_t actual2 = 0;
    size_t available2 = 0;
    DEBUG_ASSERT(max >= actual);
    s = GetVmAspaceVmos(aspace_, vmos.element_offset(actual), max - actual,
                        &actual2, &available2);
    if (s != MX_OK) {
        return s;
    }
    *actual_out = actual + actual2;
    *available_out = available + available2;
    return MX_OK;
}

mx_status_t ProcessDispatcher::GetThreads(fbl::Array<mx_koid_t>* out_threads) {
    AutoLock lock(&state_lock_);
    size_t n = thread_list_.size_slow();
    fbl::Array<mx_koid_t> threads;
    fbl::AllocChecker ac;
    threads.reset(new (&ac) mx_koid_t[n], n);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    size_t i = 0;
    for (auto& thread : thread_list_) {
        threads[i] = thread.get_koid();
        ++i;
    }
    DEBUG_ASSERT(i == n);
    *out_threads = fbl::move(threads);
    return MX_OK;
}

mx_status_t ProcessDispatcher::SetExceptionPort(fbl::RefPtr<ExceptionPort> eport) {
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

    // Lock both |state_lock_| and |exception_lock_| to ensure the process
    // doesn't transition to dead while we're setting the exception handler.
    AutoLock state_lock(&state_lock_);
    AutoLock excp_lock(&exception_lock_);
    if (state_ == State::DEAD)
        return MX_ERR_NOT_FOUND;
    if (debugger) {
        if (debugger_exception_port_)
            return MX_ERR_BAD_STATE;
        debugger_exception_port_ = eport;
    } else {
        if (exception_port_)
            return MX_ERR_BAD_STATE;
        exception_port_ = eport;
    }

    return MX_OK;
}

bool ProcessDispatcher::ResetExceptionPort(bool debugger, bool quietly) {
    LTRACE_ENTRY_OBJ;
    fbl::RefPtr<ExceptionPort> eport;

    // Remove the exception handler first. As we resume threads we don't
    // want them to hit another exception and get back into
    // ExceptionHandlerExchange.
    {
        AutoLock lock(&exception_lock_);
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
    AutoLock lock(&exception_lock_);
    return exception_port_;
}

fbl::RefPtr<ExceptionPort> ProcessDispatcher::debugger_exception_port() {
    AutoLock lock(&exception_lock_);
    return debugger_exception_port_;
}

void ProcessDispatcher::OnExceptionPortRemoval(
        const fbl::RefPtr<ExceptionPort>& eport) {
    AutoLock lock(&state_lock_);
    for (auto& thread : thread_list_) {
        thread.OnExceptionPortRemoval(eport);
    }
}

class FindProcessByKoid final : public JobEnumerator {
public:
    FindProcessByKoid(mx_koid_t koid) : koid_(koid) {}
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

    const mx_koid_t koid_;
    fbl::RefPtr<ProcessDispatcher> pd_ = nullptr;
};

// static
fbl::RefPtr<ProcessDispatcher> ProcessDispatcher::LookupProcessById(mx_koid_t koid) {
    FindProcessByKoid finder(koid);
    GetRootJobDispatcher()->EnumerateChildren(&finder, /* recurse */ true);
    return finder.get_pd();
}

fbl::RefPtr<ThreadDispatcher> ProcessDispatcher::LookupThreadById(mx_koid_t koid) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&state_lock_);

    auto iter = thread_list_.find_if([koid](const ThreadDispatcher& t) { return t.get_koid() == koid; });
    return fbl::WrapRefPtr(iter.CopyPointer());
}

uintptr_t ProcessDispatcher::get_debug_addr() const {
    AutoLock lock(&state_lock_);
    return debug_addr_;
}

mx_status_t ProcessDispatcher::set_debug_addr(uintptr_t addr) {
    if (addr == 0u)
        return MX_ERR_INVALID_ARGS;
    AutoLock lock(&state_lock_);
    // Only allow the value to be set once: Once ld.so has set it that's it.
    if (debug_addr_ != 0u)
        return MX_ERR_ACCESS_DENIED;
    debug_addr_ = addr;
    return MX_OK;
}

mx_status_t ProcessDispatcher::QueryPolicy(uint32_t condition) const {
    auto action = GetSystemPolicyManager()->QueryBasicPolicy(policy_, condition);
    if (action & MX_POL_ACTION_EXCEPTION) {
        thread_signal_policy_exception();
    }
    // TODO(cpu): check for the MX_POL_KILL bit and return an error code
    // that sysgen understands as termination.
    return (action & MX_POL_ACTION_DENY) ? MX_ERR_ACCESS_DENIED : MX_OK;
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

bool ProcessDispatcher::IsHandleValid(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return (GetHandleLocked(handle_value) != nullptr);
}
