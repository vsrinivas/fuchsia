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

#include <arch/arch_ops.h>

#include <lib/ktrace.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/handle_owner.h>
#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/debug.h>
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

constexpr uint32_t kMaxThreadStateSize = MX_MAX_THREAD_STATE_SIZE;

constexpr size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;
constexpr size_t kMaxDebugWriteBlock = 64 * 1024u * 1024u;


mx_status_t sys_thread_create(mx_handle_t process_handle,
                              user_ptr<const char> _name, uint32_t name_len,
                              uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("process handle %d, options %#x\n", process_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return ERR_INVALID_ARGS;

    // copy out the name
    char buf[MX_MAX_NAME_LEN];
    mxtl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    // TODO(andymutton): Change to use a user_ptr copy method.
    status_t result = magenta_copy_user_string(_name.get(), name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;
    LTRACEF("name %s\n", buf);

    // convert process handle to process dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    result = get_process(up, process_handle, &process);
    if (result != NO_ERROR)
        return result;

    // create the thread object
    mxtl::RefPtr<UserThread> user_thread;
    result = process->CreateUserThread(sp.data(), options, &user_thread);
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

    HandleOwner handle(MakeHandle(mxtl::move(thread_dispatcher), thread_rights));
    if (!handle)
        return ERR_NO_MEMORY;

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
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
    mx_status_t status = up->GetDispatcherWithRights(thread_handle, MX_RIGHT_WRITE,
                                                     &thread);
    if (status != NO_ERROR)
        return status;

    ktrace(TAG_THREAD_START, (uint32_t)thread->get_koid(), 0, 0, 0);
    return thread->Start(entry, stack, arg1, arg2, /* initial_thread= */ false);
}

void sys_thread_exit() {
    LTRACE_ENTRY;
    UserThread::GetCurrent()->Exit();
}

mx_status_t sys_thread_read_state(mx_handle_t handle, uint32_t state_kind,
                                  user_ptr<void> _buffer,
                                  uint32_t buffer_len, user_ptr<uint32_t> _actual) {
    LTRACEF("handle %d, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(dje): debug rights
    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
    if (status != NO_ERROR)
        return status;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    uint8_t* tmp_buf = new (&ac) uint8_t [buffer_len];
    if (!ac.check())
        return ERR_NO_MEMORY;
    mxtl::Array<uint8_t> bytes(tmp_buf, buffer_len);

    status = thread->thread()->ReadState(state_kind, bytes.get(), &buffer_len);

    // Always set the actual size so the caller can provide larger buffers.
    // The value is only usable if the status is NO_ERROR or ERR_BUFFER_TOO_SMALL.
    if (status == NO_ERROR || status == ERR_BUFFER_TOO_SMALL) {
        if (_actual.copy_to_user(buffer_len) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (status != NO_ERROR)
        return status;

    if (_buffer.copy_array_to_user(bytes.get(), buffer_len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_thread_write_state(mx_handle_t handle, uint32_t state_kind,
                                   user_ptr<const void> _buffer, uint32_t buffer_len) {
    LTRACEF("handle %d, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(dje): debug rights
    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &thread);
    if (status != NO_ERROR)
        return status;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    uint8_t* tmp_buf = new (&ac) uint8_t [buffer_len];
    if (!ac.check())
        return ERR_NO_MEMORY;
    mxtl::Array<uint8_t> bytes(tmp_buf, buffer_len);

    status = _buffer.copy_array_from_user(bytes.get(), buffer_len);
    if (status != NO_ERROR)
        return ERR_INVALID_ARGS;

    // TODO(dje): Setting privileged values in registers.
    status = thread->thread()->WriteState(state_kind, bytes.get(), buffer_len, false);
    return status;
}

mx_status_t sys_task_suspend(mx_handle_t task_handle) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(teisenbe): Add support for tasks other than threads
    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(task_handle, MX_RIGHT_WRITE,
                                                     &thread);
    if (status != NO_ERROR)
        return status;

    return thread->Suspend();
}

mx_status_t sys_process_create(mx_handle_t job_handle,
                               user_ptr<const char> _name, uint32_t name_len,
                               uint32_t options, user_ptr<mx_handle_t> _proc_handle,
                               user_ptr<mx_handle_t> _vmar_handle) {
    LTRACEF("job handle %d, options %#x\n", job_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return ERR_INVALID_ARGS;

    // copy out the name
    char buf[MX_MAX_NAME_LEN];
    mxtl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    // TODO(andymutton): Change to use user_ptr copy methods
    status_t result = magenta_copy_user_string(_name.get(), name_len, buf, sizeof(buf), &sp);
    if (result != NO_ERROR)
        return result;
    LTRACEF("name %s\n", buf);

    // convert job handle to job dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<JobDispatcher> job;
    if (job_handle != MX_HANDLE_INVALID) {
        // TODO: don't accept invalid handle here.
        // TODO: define process creation job rights.
        auto status = up->GetDispatcherWithRights(job_handle, MX_RIGHT_WRITE, &job);
        if (status != NO_ERROR)
            return status;
    }

    // create a new process dispatcher
    mxtl::RefPtr<Dispatcher> proc_dispatcher;
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar_dispatcher;
    mx_rights_t proc_rights, vmar_rights;
    status_t res = ProcessDispatcher::Create(mxtl::move(job), sp, options,
                                             &proc_dispatcher, &proc_rights,
                                             &vmar_dispatcher, &vmar_rights);
    if (res != NO_ERROR)
        return res;

    uint32_t koid = (uint32_t)proc_dispatcher->get_koid();
    ktrace(TAG_PROC_CREATE, koid, 0, 0, 0);
    ktrace_name(TAG_PROC_NAME, koid, 0, buf);

    // Give arch-specific tracing a chance to record process creation.
    arch_trace_process_create(koid, &vmar_dispatcher->vmar()->aspace()->arch_aspace());

    // Create a handle and attach the dispatcher to it
    HandleOwner proc_h(MakeHandle(mxtl::move(proc_dispatcher), proc_rights));
    if (!proc_h)
        return ERR_NO_MEMORY;

    // Create a handle and attach the dispatcher to it
    HandleOwner vmar_h(MakeHandle(mxtl::move(vmar_dispatcher), vmar_rights));
    if (!vmar_h)
        return ERR_NO_MEMORY;

    if (_proc_handle.copy_to_user(up->MapHandleToValue(proc_h)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (_vmar_handle.copy_to_user(up->MapHandleToValue(vmar_h)) != NO_ERROR)
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
    status = up->GetDispatcherWithRights(thread_handle, MX_RIGHT_WRITE, &thread);
    if (status != NO_ERROR)
        return status;

    // test that the thread belongs to the starting process
    if (thread->thread()->process() != process.get())
        return ERR_ACCESS_DENIED;

    // XXX test that handle has TRANSFER rights before we remove it from the source process

    HandleOwner arg_handle = up->RemoveHandle(arg_handle_value);
    if (!arg_handle)
        return ERR_INVALID_ARGS;

    auto arg_nhv = process->MapHandleToValue(arg_handle);
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

mx_status_t sys_process_read_memory(mx_handle_t proc, uintptr_t vaddr,
                                    user_ptr<void> _buffer,
                                    size_t len, user_ptr<size_t> _actual) {
    if (!_buffer)
        return ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugReadBlock)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcherWithRights(proc, MX_RIGHT_READ | MX_RIGHT_WRITE,
                                                     &process);
    if (status != NO_ERROR)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return ERR_NO_MEMORY;

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    size_t read = 0;

    status_t st = vmo->ReadUser(_buffer, offset, len, &read);

    if (st == NO_ERROR) {
        if (_actual.copy_to_user(static_cast<size_t>(read)) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    return st;
}

mx_status_t sys_process_write_memory(mx_handle_t proc, uintptr_t vaddr,
                                     user_ptr<const void> _buffer,
                                     size_t len, user_ptr<size_t> _actual) {
    if (!_buffer)
        return ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugWriteBlock)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcherWithRights(proc, MX_RIGHT_WRITE, &process);
    if (status != NO_ERROR)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return ERR_NO_MEMORY;

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    size_t written = 0;

    status_t st = vmo->WriteUser(_buffer, offset, len, &written);

    if (st == NO_ERROR) {
        if (_actual.copy_to_user(static_cast<size_t>(written)) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    return st;
}

// helper routine for sys_task_kill
template <typename T>
static mx_status_t kill_task(mxtl::RefPtr<Dispatcher> dispatcher) {
    auto task = DownCastDispatcher<T>(&dispatcher);
    if (!task)
        return ERR_WRONG_TYPE;

    task->Kill();
    return NO_ERROR;
}

mx_status_t sys_task_kill(mx_handle_t task_handle) {
    LTRACEF("handle %d\n", task_handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(task_handle, MX_RIGHT_WRITE, &dispatcher);
    if (status != NO_ERROR)
        return status;

    // see if it's a process or thread and dispatch accordingly
    switch (dispatcher->get_type()) {
        case MX_OBJ_TYPE_PROCESS:
            return kill_task<ProcessDispatcher>(mxtl::move(dispatcher));
        case MX_OBJ_TYPE_THREAD:
            return kill_task<ThreadDispatcher>(mxtl::move(dispatcher));
        case MX_OBJ_TYPE_JOB:
            return kill_task<JobDispatcher>(mxtl::move(dispatcher));
        default:
            return ERR_WRONG_TYPE;
    }
}

mx_status_t sys_job_create(mx_handle_t parent_job, uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("parent: %d\n", parent_job);

    if (options != 0u)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<JobDispatcher> parent;
    mx_status_t status = up->GetDispatcherWithRights(parent_job, MX_RIGHT_WRITE, &parent);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> job;
    mx_rights_t rights;
    status = JobDispatcher::Create(options, mxtl::move(parent), &job, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner job_handle(MakeHandle(mxtl::move(job), rights));
    if (_out.copy_to_user(up->MapHandleToValue(job_handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(job_handle));
    return NO_ERROR;
}
