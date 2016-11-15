// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <new.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <kernel/thread.h>

#include <lib/ktrace.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>
#include <magenta/vm_address_region_dispatcher.h>

#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

extern "C" {
uint64_t get_tsc_ticks_per_ms(void);
};

mx_status_t sys_thread_create(mx_handle_t process_handle,
                              user_ptr<const char> name, uint32_t name_len,
                              uint32_t flags, user_ptr<mx_handle_t> out) {
    LTRACEF("process handle %d, flags %#x\n", process_handle, flags);

    // copy the name to a local buffer
    char buf[MX_MAX_NAME_LEN];
    mxtl::StringPiece sp;
    status_t result = magenta_copy_user_string(name.get(), name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;

    // currently, the only valid flag value is 0
    if (flags != 0)
        return ERR_INVALID_ARGS;

    // convert process handle to process dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    result = get_process(up, process_handle, &process);
    if (result != NO_ERROR)
        return result;

    // create the thread object
    mxtl::RefPtr<UserThread> user_thread;
    result = process->CreateUserThread(sp.data(), flags, &user_thread);
    if (result != NO_ERROR)
        return result;

    // create the thread dispatcher
    mxtl::RefPtr<Dispatcher> thread_dispatcher;
    mx_rights_t thread_rights;
    result = ThreadDispatcher::Create(mxtl::move(user_thread), &thread_dispatcher, &thread_rights);
    if (result != NO_ERROR)
        return result;

    uint32_t tid = (uint32_t)thread_dispatcher->get_koid();
    uint32_t pid = (uint32_t)process->get_koid();
    ktrace(TAG_THREAD_CREATE, tid, pid, 0, 0);
    ktrace_name(TAG_THREAD_NAME, tid, pid, buf);

    HandleUniquePtr handle(MakeHandle(mxtl::move(thread_dispatcher), thread_rights));
    if (!handle)
        return ERR_NO_MEMORY;

    if (out.copy_to_user(up->MapHandleToValue(handle.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    return NO_ERROR;
}

mx_status_t sys_thread_start(mx_handle_t thread_handle, uintptr_t entry,
                             uintptr_t stack, uintptr_t arg1, uintptr_t arg2) {
    LTRACEF("handle %d, entry %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg1 %#" PRIxPTR ", arg2 %#" PRIxPTR "\n",
            thread_handle, entry, stack, arg1, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcher(thread_handle, &thread,
                                           MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    ktrace(TAG_THREAD_START, (uint32_t)thread->get_koid(), 0, 0, 0);
    return thread->Start(entry, stack, arg1, arg2, /* initial_thread= */ false);
}

void sys_thread_exit() {
    LTRACE_ENTRY;
    UserThread::GetCurrent()->Exit();
}

mx_status_t sys_thread_arch_prctl(mx_handle_t handle_value, uint32_t op,
                                  user_ptr<uintptr_t> value_ptr) {
    LTRACEF("handle %d operation %u value_ptr %p", handle_value, op, value_ptr.get());

    // TODO(cpu) what to do with |handle_value|?

    uintptr_t value;

    switch (op) {
#ifdef ARCH_X86_64
    case ARCH_SET_FS:
        if (value_ptr.copy_from_user(&value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        if (!x86_is_vaddr_canonical(value))
            return ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_FS_BASE, value);
        break;
    case ARCH_GET_FS:
        value = read_msr(X86_MSR_IA32_FS_BASE);
        if (value_ptr.copy_to_user(value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
    case ARCH_SET_GS:
        if (value_ptr.copy_from_user(&value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        if (!x86_is_vaddr_canonical(value))
            return ERR_INVALID_ARGS;
        write_msr(X86_MSR_IA32_KERNEL_GS_BASE, value);
        break;
    case ARCH_GET_GS:
        value = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
        if (value_ptr.copy_to_user(value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
    case ARCH_GET_TSC_TICKS_PER_MS:
        value = get_tsc_ticks_per_ms();
        if (value_ptr.copy_to_user(value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        break;
#elif ARCH_ARM64
    case ARCH_SET_TPIDRRO_EL0:
        if (value_ptr.copy_from_user(&value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        ARM64_WRITE_SYSREG(tpidrro_el0, value);
        break;
#elif ARCH_ARM
    case ARCH_SET_CP15_READONLY:
        if (value_ptr.copy_from_user(&value) != NO_ERROR)
            return ERR_INVALID_ARGS;
        __asm__ volatile("mcr p15, 0, %0, c13, c0, 3" : : "r" (value));
        ISB;
        break;
#endif
    default:
        return ERR_INVALID_ARGS;
    }

    return NO_ERROR;
}

mx_status_t sys_process_create(mx_handle_t job_handle,
                               user_ptr<const char> name, uint32_t name_len,
                               uint32_t flags, user_ptr<mx_handle_t> proc_handle,
                               user_ptr<mx_handle_t> vmar_handle) {
    LTRACEF("name %p, flags 0x%x\n", name.get(), flags);

    // currently, the only valid flag value is 0
    if (flags != 0)
        return ERR_INVALID_ARGS;

    // copy out the name
    char buf[MX_MAX_NAME_LEN];
    mxtl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    status_t result = magenta_copy_user_string(name.get(), name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;
    LTRACEF("name %s\n", buf);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<JobDispatcher> job;
    if (job_handle != MX_HANDLE_INVALID) {
        // TODO: don't accept invalid handle here.
        // TODO: define process creation job rights.
        auto status = up->GetDispatcher(job_handle, &job, MX_RIGHT_WRITE);
        if (status != NO_ERROR)
            return status;
    }

    // create a new process dispatcher
    mxtl::RefPtr<Dispatcher> proc_dispatcher;
    mx_rights_t proc_rights;
    status_t res = ProcessDispatcher::Create(mxtl::move(job), sp, &proc_dispatcher, &proc_rights, flags);
    if (res != NO_ERROR)
        return res;

    mxtl::RefPtr<Dispatcher> vmar_dispatcher;
    mx_rights_t vmar_rights;
    {
        mxtl::RefPtr<ProcessDispatcher> process_dispatcher(
                proc_dispatcher->get_specific<ProcessDispatcher>());
        ASSERT(process_dispatcher);

        // Create a dispatcher for the root VMAR
        mxtl::RefPtr<VmAddressRegion> root_vmar(process_dispatcher->aspace()->root_vmar());
        status_t status = VmAddressRegionDispatcher::Create(mxtl::move(root_vmar),
                                                            &vmar_dispatcher, &vmar_rights);
        if (status != NO_ERROR)
            return status;
    }

    uint32_t koid = (uint32_t)proc_dispatcher->get_koid();
    ktrace(TAG_PROC_CREATE, koid, 0, 0, 0);
    ktrace_name(TAG_PROC_NAME, koid, 0, buf);

    // Create a handle and attach the dispatcher to it
    HandleUniquePtr proc_h(MakeHandle(mxtl::move(proc_dispatcher), proc_rights));
    if (!proc_h)
        return ERR_NO_MEMORY;

    // Create a handle and attach the dispatcher to it
    HandleUniquePtr vmar_h(MakeHandle(mxtl::move(vmar_dispatcher), vmar_rights));
    if (!vmar_h)
        return ERR_NO_MEMORY;

    if (proc_handle.copy_to_user(up->MapHandleToValue(proc_h.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (vmar_handle.copy_to_user(up->MapHandleToValue(vmar_h.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(vmar_h));
    up->AddHandle(mxtl::move(proc_h));

    return NO_ERROR;
}

// Note: This is used to start the main thread (as opposed to using
// sys_thread_start for that) for a few reasons:
// - less easily exploitable
//   We want to make sure we can't generically transfer handles to a process.
//   This has the nice property of restricting the evil (transferring handle
//   to new process) to exactly one spot, and can be called exactly once per
//   process, since it also pushes it into a new state.
// - maintains the state machine invariant that 'started' processes have one
//   thread running

mx_status_t sys_process_start(mx_handle_t process_handle, mx_handle_t thread_handle,
                              uintptr_t pc, uintptr_t sp,
                              mx_handle_t arg_handle_value, uintptr_t arg2) {
    LTRACEF("phandle %d, thandle %d, pc %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg_handle %d, arg2 %#" PRIxPTR "\n",
            process_handle, thread_handle, pc, sp, arg_handle_value, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    // get process dispatcher
    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = get_process(up, process_handle, &process);
    if (status != NO_ERROR)
        return status;

    // get thread_dispatcher
    mxtl::RefPtr<ThreadDispatcher> thread;
    status = up->GetDispatcher(thread_handle, &thread, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // test that the thread belongs to the starting process
    if (thread->thread()->process() != process.get())
        return ERR_ACCESS_DENIED;

    // XXX test that handle has TRANSFER rights before we remove it from the source process

    HandleUniquePtr arg_handle = up->RemoveHandle(arg_handle_value);
    if (!arg_handle)
        return ERR_INVALID_ARGS;

    auto arg_nhv = process->MapHandleToValue(arg_handle.get());
    process->AddHandle(mxtl::move(arg_handle));

    // TODO(cpu) if Start() fails we want to undo RemoveHandle().

    ktrace(TAG_PROC_START, (uint32_t)thread->get_koid(),
           (uint32_t)process->get_koid(), 0, 0);

    return thread->Start(pc, sp, arg_nhv, arg2, /* initial_thread= */ true);
}

void sys_process_exit(int retcode) {
    LTRACEF("retcode %d\n", retcode);
    ProcessDispatcher::GetCurrent()->Exit(retcode);
}

// helper routine for sys_task_kill
template <typename T>
static mx_status_t kill_task(mxtl::RefPtr<Dispatcher> dispatcher, uint32_t rights) {
    auto task = dispatcher->get_specific<T>();
    if (!task)
        return ERR_WRONG_TYPE;

    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    task->Kill();
    return NO_ERROR;
}

mx_status_t sys_task_kill(mx_handle_t task_handle) {
    LTRACEF("handle %d\n", task_handle);

    auto up = ProcessDispatcher::GetCurrent();

    // get dispatcher to the handle passed in
    // use the bool version of GetDispatcher to just get a raw dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(task_handle, &dispatcher, &rights))
        return up->BadHandle(task_handle, ERR_BAD_HANDLE);

    // see if it's a process or thread and dispatch accordingly
    switch (dispatcher->get_type()) {
        case MX_OBJ_TYPE_PROCESS:
            return kill_task<ProcessDispatcher>(mxtl::move(dispatcher), rights);
        case MX_OBJ_TYPE_THREAD:
            return kill_task<ThreadDispatcher>(mxtl::move(dispatcher), rights);
        default:
            return ERR_WRONG_TYPE;
    }
}

mx_status_t sys_job_create(mx_handle_t parent_job, uint32_t flags, user_ptr<mx_handle_t> out) {
    LTRACEF("parent: %d\n", parent_job);

    if (flags != 0u)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<JobDispatcher> parent;
    mx_status_t status = up->GetDispatcher(parent_job, &parent, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> job;
    mx_rights_t rights;
    status = JobDispatcher::Create(flags, mxtl::move(parent), &job, &rights);
    if (status != NO_ERROR)
        return status;

    HandleUniquePtr job_handle(MakeHandle(mxtl::move(job), rights));
    if (out.copy_to_user(up->MapHandleToValue(job_handle.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(job_handle));
    return NO_ERROR;
}
