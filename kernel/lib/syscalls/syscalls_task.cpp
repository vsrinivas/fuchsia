// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <arch/arch_ops.h>

#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>
#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/policy.h>
#include <fbl/auto_lock.h>
#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0
#define THREAD_SET_PRIORITY_EXPERIMENT 1

#if THREAD_SET_PRIORITY_EXPERIMENT
#include <kernel/cmdline.h>
#include <kernel/thread.h>
#include <lk/init.h>
#endif

// For reading general purpose integer registers, we can allocate in
// an inline array and save the malloc. Assume 64 registers as a
// conservative estimate for an architecture with 32 general purpose
// integer registers.
constexpr uint32_t kInlineThreadStateSize = sizeof(void*) * 64;
constexpr uint32_t kMaxThreadStateSize = MX_MAX_THREAD_STATE_SIZE;

constexpr size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;
constexpr size_t kMaxDebugWriteBlock = 64 * 1024u * 1024u;

// Assume the typical set-policy call has 8 items or less.
constexpr size_t kPolicyBasicInlineCount = 8;

#if THREAD_SET_PRIORITY_EXPERIMENT
// See MG-940
static bool thread_set_priority_allowed = false;
static void thread_set_priority_experiment_init_hook(uint) {
    thread_set_priority_allowed = cmdline_get_bool("thread.set.priority.allowed", false);
    printf("thread set priority experiment is : %s\n",
           thread_set_priority_allowed ? "ENABLED" : "DISABLED");
}
LK_INIT_HOOK(thread_set_priority_experiment,
             thread_set_priority_experiment_init_hook,
             LK_INIT_LEVEL_THREADING - 1);
#endif

// TODO(MG-1025): copy_user_string may truncate the incoming string,
// and may copy extra data past the NUL.
// TODO(dbort): If anyone else needs this, move it into user_ptr.
static mx_status_t copy_user_string(const user_ptr<const char>& src,
                                    size_t src_len,
                                    char* buf, size_t buf_len,
                                    fbl::StringPiece* sp) {
    if (!src || src_len > buf_len) {
        return MX_ERR_INVALID_ARGS;
    }
    mx_status_t result = src.copy_array_from_user(buf, src_len);
    if (result != MX_OK) {
        return MX_ERR_INVALID_ARGS;
    }

    // ensure zero termination
    size_t str_len = (src_len == buf_len ? src_len - 1 : src_len);
    buf[str_len] = 0;
    *sp = fbl::StringPiece(buf);

    return MX_OK;
}

// Convenience function to go from process handle to process.
static mx_status_t get_process(ProcessDispatcher* up,
                               mx_handle_t proc_handle,
                               fbl::RefPtr<ProcessDispatcher>* proc) {
    return up->GetDispatcherWithRights(proc_handle, MX_RIGHT_WRITE, proc);
}

mx_status_t sys_thread_create(mx_handle_t process_handle,
                              user_ptr<const char> _name, uint32_t name_len,
                              uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("process handle %x, options %#x\n", process_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return MX_ERR_INVALID_ARGS;

    // copy out the name
    char buf[MX_MAX_NAME_LEN];
    fbl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    mx_status_t result = copy_user_string(_name, name_len,
                                          buf, sizeof(buf), &sp);
    if (result != MX_OK)
        return result;
    LTRACEF("name %s\n", buf);

    // convert process handle to process dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    result = get_process(up, process_handle, &process);
    if (result != MX_OK)
        return result;

    uint32_t pid = (uint32_t)process->get_koid();

    // create the thread dispatcher
    fbl::RefPtr<Dispatcher> thread_dispatcher;
    mx_rights_t thread_rights;
    result = ThreadDispatcher::Create(fbl::move(process), options, sp,
                                      &thread_dispatcher, &thread_rights);
    if (result != MX_OK)
        return result;

    uint32_t tid = (uint32_t)thread_dispatcher->get_koid();
    ktrace(TAG_THREAD_CREATE, tid, pid, 0, 0);
    ktrace_name(TAG_THREAD_NAME, tid, pid, buf);

    HandleOwner handle(MakeHandle(fbl::move(thread_dispatcher), thread_rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    up->AddHandle(fbl::move(handle));

    return MX_OK;
}

mx_status_t sys_thread_start(mx_handle_t thread_handle, uintptr_t entry,
                             uintptr_t stack, uintptr_t arg1, uintptr_t arg2) {
    LTRACEF("handle %x, entry %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg1 %#" PRIxPTR ", arg2 %#" PRIxPTR "\n",
            thread_handle, entry, stack, arg1, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(thread_handle, MX_RIGHT_WRITE,
                                                     &thread);
    if (status != MX_OK)
        return status;

    ktrace(TAG_THREAD_START, (uint32_t)thread->get_koid(), 0, 0, 0);
    return thread->Start(entry, stack, arg1, arg2, /* initial_thread= */ false);
}

void sys_thread_exit() {
    LTRACE_ENTRY;
    ThreadDispatcher::GetCurrent()->Exit();
}

mx_status_t sys_thread_read_state(mx_handle_t handle, uint32_t state_kind,
                                  user_ptr<void> _buffer,
                                  uint32_t buffer_len, user_ptr<uint32_t> _actual) {
    LTRACEF("handle %x, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(MG-968): debug rights
    fbl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &thread);
    if (status != MX_OK)
        return status;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    fbl::InlineArray<uint8_t, kInlineThreadStateSize> bytes(&ac, buffer_len);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    status = thread->ReadState(state_kind, bytes.get(), &buffer_len);

    // Always set the actual size so the caller can provide larger buffers.
    // The value is only usable if the status is MX_OK or MX_ERR_BUFFER_TOO_SMALL.
    if (status == MX_OK || status == MX_ERR_BUFFER_TOO_SMALL) {
        if (_actual.copy_to_user(buffer_len) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }

    if (status != MX_OK)
        return status;

    if (_buffer.copy_array_to_user(bytes.get(), buffer_len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return MX_OK;
}

mx_status_t sys_thread_write_state(mx_handle_t handle, uint32_t state_kind,
                                   user_ptr<const void> _buffer, uint32_t buffer_len) {
    LTRACEF("handle %x, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(MG-968): debug rights
    fbl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &thread);
    if (status != MX_OK)
        return status;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    fbl::InlineArray<uint8_t, kInlineThreadStateSize> bytes(&ac, buffer_len);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    status = _buffer.copy_array_from_user(bytes.get(), buffer_len);
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;

    status = thread->WriteState(state_kind, bytes.get(), buffer_len);
    return status;
}

// See MG-940
mx_status_t sys_thread_set_priority(int32_t prio) {
#if THREAD_SET_PRIORITY_EXPERIMENT
    // If the experimental mx_thread_set_priority has not been enabled using the
    // kernel command line option, simply deny this request.
    if (!thread_set_priority_allowed)
        return MX_ERR_NOT_SUPPORTED;

    if ((prio < LOWEST_PRIORITY) || (prio > HIGHEST_PRIORITY))
        return MX_ERR_INVALID_ARGS;

    thread_set_priority(prio);

    return MX_OK;
#else
    return MX_ERR_NOT_SUPPORTED;
#endif
}

mx_status_t sys_task_suspend(mx_handle_t task_handle) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(teisenbe): Add support for tasks other than threads
    fbl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcherWithRights(task_handle, MX_RIGHT_WRITE,
                                                     &thread);
    if (status != MX_OK)
        return status;

    return thread->Suspend();
}

mx_status_t sys_process_create(mx_handle_t job_handle,
                               user_ptr<const char> _name, uint32_t name_len,
                               uint32_t options, user_ptr<mx_handle_t> _proc_handle,
                               user_ptr<mx_handle_t> _vmar_handle) {
    LTRACEF("job handle %x, options %#x\n", job_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return MX_ERR_INVALID_ARGS;

    // copy out the name
    char buf[MX_MAX_NAME_LEN];
    fbl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    mx_status_t result = copy_user_string(_name, name_len,
                                          buf, sizeof(buf), &sp);
    if (result != MX_OK)
        return result;
    LTRACEF("name %s\n", buf);

    // convert job handle to job dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> job;
    // TODO(MG-968): define process creation job rights.
    auto status = up->GetDispatcherWithRights(job_handle, MX_RIGHT_WRITE, &job);
    if (status != MX_OK)
        return status;

    // create a new process dispatcher
    fbl::RefPtr<Dispatcher> proc_dispatcher;
    fbl::RefPtr<VmAddressRegionDispatcher> vmar_dispatcher;
    mx_rights_t proc_rights, vmar_rights;
    mx_status_t res = ProcessDispatcher::Create(fbl::move(job), sp, options,
                                                &proc_dispatcher, &proc_rights,
                                                &vmar_dispatcher, &vmar_rights);
    if (res != MX_OK)
        return res;

    uint32_t koid = (uint32_t)proc_dispatcher->get_koid();
    ktrace(TAG_PROC_CREATE, koid, 0, 0, 0);
    ktrace_name(TAG_PROC_NAME, koid, 0, buf);

    // Give arch-specific tracing a chance to record process creation.
    arch_trace_process_create(koid, vmar_dispatcher->vmar()->aspace()->arch_aspace().arch_table_phys());

    // Create a handle and attach the dispatcher to it
    HandleOwner proc_h(MakeHandle(fbl::move(proc_dispatcher), proc_rights));
    if (!proc_h)
        return MX_ERR_NO_MEMORY;

    // Create a handle and attach the dispatcher to it
    HandleOwner vmar_h(MakeHandle(fbl::move(vmar_dispatcher), vmar_rights));
    if (!vmar_h)
        return MX_ERR_NO_MEMORY;

    if (_proc_handle.copy_to_user(up->MapHandleToValue(proc_h)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (_vmar_handle.copy_to_user(up->MapHandleToValue(vmar_h)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(vmar_h));
    up->AddHandle(fbl::move(proc_h));

    return MX_OK;
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
    LTRACEF("phandle %x, thandle %x, pc %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg_handle %x, arg2 %#" PRIxPTR "\n",
            process_handle, thread_handle, pc, sp, arg_handle_value, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    // get process dispatcher
    fbl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = get_process(up, process_handle, &process);
    if (status != MX_OK)
        return status;

    // get thread_dispatcher
    fbl::RefPtr<ThreadDispatcher> thread;
    status = up->GetDispatcherWithRights(thread_handle, MX_RIGHT_WRITE, &thread);
    if (status != MX_OK)
        return status;

    // test that the thread belongs to the starting process
    if (thread->process() != process.get())
        return MX_ERR_ACCESS_DENIED;

    HandleOwner arg_handle;
    {
        fbl::AutoLock lock(up->handle_table_lock());
        auto handle = up->GetHandleLocked(arg_handle_value);
        if (!handle)
            return MX_ERR_BAD_HANDLE;
        if (!handle->HasRights(MX_RIGHT_TRANSFER))
            return MX_ERR_ACCESS_DENIED;
        arg_handle = up->RemoveHandleLocked(arg_handle_value);
    }

    auto arg_nhv = process->MapHandleToValue(arg_handle);
    process->AddHandle(fbl::move(arg_handle));

    status = thread->Start(pc, sp, arg_nhv, arg2, /* initial_thread */ true);
    if (status != MX_OK) {
        // Put back the |arg_handle| into the calling process.
        auto handle = process->RemoveHandle(arg_nhv);
        up->AddHandle(fbl::move(handle));
        return status;
    }

    ktrace(TAG_PROC_START, (uint32_t)thread->get_koid(),
           (uint32_t)process->get_koid(), 0, 0);

    return MX_OK;
}

void sys_process_exit(int retcode) {
    LTRACEF("retcode %d\n", retcode);
    ProcessDispatcher::GetCurrent()->Exit(retcode);
}

mx_status_t sys_process_read_memory(mx_handle_t proc, uintptr_t vaddr,
                                    user_ptr<void> _buffer,
                                    size_t len, user_ptr<size_t> _actual) {
    LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, len);

    if (!_buffer)
        return MX_ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugReadBlock)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcherWithRights(proc, MX_RIGHT_READ | MX_RIGHT_WRITE,
                                                     &process);
    if (status != MX_OK)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return MX_ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return MX_ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return MX_ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return MX_ERR_NO_MEMORY;

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    size_t read = 0;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(MG-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = _buffer.reinterpret<uint8_t>();
        for (size_t i = 0; i < len; i += PAGE_SIZE) {
            status = int_data.copy_array_to_user(&byte, 1, i);
            if (status != MX_OK) {
                return status;
            }
        }
        if (len > 0) {
            status = int_data.copy_array_to_user(&byte, 1, len - 1);
            if (status != MX_OK) {
                return status;
            }
        }
    }

    mx_status_t st = vmo->ReadUser(_buffer, offset, len, &read);

    if (st == MX_OK) {
        if (_actual.copy_to_user(static_cast<size_t>(read)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    return st;
}

mx_status_t sys_process_write_memory(mx_handle_t proc, uintptr_t vaddr,
                                     user_ptr<const void> _buffer,
                                     size_t len, user_ptr<size_t> _actual) {
    LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, len);

    if (!_buffer)
        return MX_ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugWriteBlock)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcherWithRights(proc, MX_RIGHT_WRITE, &process);
    if (status != MX_OK)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return MX_ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return MX_ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return MX_ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return MX_ERR_NO_MEMORY;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(MG-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = _buffer.reinterpret<const uint8_t>();
        for (size_t i = 0; i < len; i += PAGE_SIZE) {
            status = int_data.copy_array_from_user(&byte, 1, i);
            if (status != MX_OK) {
                return status;
            }
        }
        if (len > 0) {
            status = int_data.copy_array_from_user(&byte, 1, len - 1);
            if (status != MX_OK) {
                return status;
            }
        }
    }

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    size_t written = 0;

    mx_status_t st = vmo->WriteUser(_buffer, offset, len, &written);

    if (st == MX_OK) {
        if (_actual.copy_to_user(static_cast<size_t>(written)) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    return st;
}

// helper routine for sys_task_kill
template <typename T>
static mx_status_t kill_task(fbl::RefPtr<Dispatcher> dispatcher) {
    auto task = DownCastDispatcher<T>(&dispatcher);
    if (!task)
        return MX_ERR_WRONG_TYPE;

    task->Kill();
    return MX_OK;
}

mx_status_t sys_task_kill(mx_handle_t task_handle) {
    LTRACEF("handle %x\n", task_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(task_handle, MX_RIGHT_DESTROY, &dispatcher);
    if (status != MX_OK)
        return status;

    // see if it's a process or thread and dispatch accordingly
    switch (dispatcher->get_type()) {
        case MX_OBJ_TYPE_PROCESS:
            return kill_task<ProcessDispatcher>(fbl::move(dispatcher));
        case MX_OBJ_TYPE_THREAD:
            return kill_task<ThreadDispatcher>(fbl::move(dispatcher));
        case MX_OBJ_TYPE_JOB:
            return kill_task<JobDispatcher>(fbl::move(dispatcher));
        default:
            return MX_ERR_WRONG_TYPE;
    }
}

mx_status_t sys_job_create(mx_handle_t parent_job, uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("parent: %x\n", parent_job);

    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> parent;
    mx_status_t status = up->GetDispatcherWithRights(parent_job, MX_RIGHT_WRITE, &parent);
    if (status != MX_OK)
        return status;

    fbl::RefPtr<Dispatcher> job;
    mx_rights_t rights;
    status = JobDispatcher::Create(options, fbl::move(parent), &job, &rights);
    if (status != MX_OK)
        return status;

    HandleOwner job_handle(MakeHandle(fbl::move(job), rights));
    if (_out.copy_to_user(up->MapHandleToValue(job_handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(job_handle));
    return MX_OK;
}

mx_status_t sys_job_set_policy(mx_handle_t job_handle, uint32_t options,
    uint32_t topic, user_ptr<const void> _policy, uint32_t count) {

    if ((options != MX_JOB_POL_RELATIVE) && (options != MX_JOB_POL_ABSOLUTE))
        return MX_ERR_INVALID_ARGS;
    if (!_policy || (count == 0u))
        return MX_ERR_INVALID_ARGS;

    if (topic != MX_JOB_POL_BASIC)
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    fbl::InlineArray<
        mx_policy_basic, kPolicyBasicInlineCount> policy(&ac, count);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto status = _policy.copy_array_from_user(policy.get(), sizeof(mx_policy_basic) * count);
    if (status != MX_OK)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> job;
    status = up->GetDispatcherWithRights(job_handle, MX_RIGHT_SET_POLICY, &job);
    if (status != MX_OK)
        return status;

    return job->SetPolicy(options, policy.get(), policy.size());
}

mx_status_t sys_job_set_relative_importance(
    mx_handle_t resource_handle,
    mx_handle_t job_handle, mx_handle_t less_important_job_handle) {

    ProcessDispatcher* up = ProcessDispatcher::GetCurrent();

    // If the caller has a valid handle to the root resource, let them perform
    // this operation no matter the rights on the job handles.
    {
        fbl::RefPtr<ResourceDispatcher> resource;
        mx_status_t status = up->GetDispatcherWithRights(
            resource_handle, MX_RIGHT_NONE, &resource);
        if (status != MX_OK)
            return status;
        // TODO(MG-971): Check that this is actually the appropriate resource
    }

    // Get the job to modify.
    fbl::RefPtr<JobDispatcher> job;
    mx_status_t status = up->GetDispatcherWithRights(
        job_handle, MX_RIGHT_NONE, &job);
    if (status != MX_OK)
        return status;

    // Get its less-important neighbor, or null.
    fbl::RefPtr<JobDispatcher> li_job;
    if (less_important_job_handle != MX_HANDLE_INVALID) {
        status = up->GetDispatcherWithRights(
            less_important_job_handle, MX_RIGHT_NONE, &li_job);
        if (status != MX_OK)
            return status;
    }

    return job->MakeMoreImportantThan(fbl::move(li_job));
}
