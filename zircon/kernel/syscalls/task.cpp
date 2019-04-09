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
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/suspend_token_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

#include <fbl/auto_lock.h>
#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/policy.h>

#include "priv.h"

#define LOCAL_TRACE 0

namespace {

constexpr size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;
constexpr size_t kMaxDebugWriteBlock = 64 * 1024u * 1024u;

// Assume the typical set-policy call has 8 items or less.
constexpr size_t kPolicyBasicInlineCount = 8;

// TODO(ZX-1025): copy_user_string may truncate the incoming string,
// and may copy extra data past the NUL.
// TODO(dbort): If anyone else needs this, move it into user_ptr.
zx_status_t copy_user_string(const user_in_ptr<const char>& src,
                             size_t src_len,
                             char* buf, size_t buf_len,
                             fbl::StringPiece* sp) {
    if (!src || src_len > buf_len) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t result = src.copy_array_from_user(buf, src_len);
    if (result != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }

    // ensure zero termination
    size_t str_len = (src_len == buf_len ? src_len - 1 : src_len);
    buf[str_len] = 0;
    *sp = fbl::StringPiece(buf);

    return ZX_OK;
}

// This represents the local storage for thread_read/write_state. It should be large enough to
// handle all structures passed over these APIs.
union thread_state_local_buffer_t {
    zx_thread_state_general_regs_t general_regs;  // ZX_THREAD_STATE_GENERAL_REGS
    zx_thread_state_fp_regs_t fp_regs;            // ZX_THREAD_STATE_FP_REGS
    zx_thread_state_vector_regs_t vector_regs;    // ZX_THREAD_STATE_VECTOR_REGS
    zx_thread_state_debug_regs_t debug_regs;      // ZX_THREAD_STATE_DEBUG_REGS
    uint32_t single_step;                         // ZX_THREAD_STATE_SINGLE_STEP
    uint64_t x86_register_fs;                     // ZX_THREAD_X86_REGISTER_FS;
    uint64_t x86_register_gs;                     // ZX_THREAD_X86_REGISTER_GS;
};

// Validates the input topic to thread_read_state and thread_write_state is a valid value, and
// checks that the input buffer size is at least as large as necessary for the topic. On ZX_OK, the
// actual size necessary for the buffer will be placed in the output parameter.
zx_status_t validate_thread_state_input(uint32_t in_topic, size_t in_len, size_t* out_len) {
    switch (in_topic) {
    case ZX_THREAD_STATE_GENERAL_REGS:
        *out_len = sizeof(zx_thread_state_general_regs_t);
        break;
    case ZX_THREAD_STATE_FP_REGS:
        *out_len = sizeof(zx_thread_state_fp_regs_t);
        break;
    case ZX_THREAD_STATE_VECTOR_REGS:
        *out_len = sizeof(zx_thread_state_vector_regs_t);
        break;
    case ZX_THREAD_STATE_DEBUG_REGS:
        *out_len = sizeof(zx_thread_state_debug_regs_t);
        break;
    case ZX_THREAD_STATE_SINGLE_STEP:
        *out_len = sizeof(zx_thread_state_single_step_t);
        break;
    case ZX_THREAD_X86_REGISTER_FS:
        *out_len = sizeof(zx_thread_x86_register_fs_t);
        break;
    case ZX_THREAD_X86_REGISTER_GS:
        *out_len = sizeof(zx_thread_x86_register_gs_t);
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    if (in_len < *out_len)
        return ZX_ERR_BUFFER_TOO_SMALL;
    return ZX_OK;
}

} // namespace

// zx_status_t zx_thread_create
zx_status_t sys_thread_create(zx_handle_t process_handle,
                              user_in_ptr<const char> _name, size_t name_len,
                              uint32_t options, user_out_handle* out) {
    LTRACEF("process handle %x, options %#x\n", process_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return ZX_ERR_INVALID_ARGS;

    // copy out the name
    char buf[ZX_MAX_NAME_LEN];
    fbl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    zx_status_t result = copy_user_string(_name, name_len,
                                          buf, sizeof(buf), &sp);
    if (result != ZX_OK)
        return result;
    LTRACEF("name %s\n", buf);

    // convert process handle to process dispatcher
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    result = up->GetDispatcherWithRights(process_handle, ZX_RIGHT_MANAGE_THREAD, &process);
    if (result != ZX_OK) {
        return result;
    }

    uint32_t pid = (uint32_t)process->get_koid();

    // create the thread dispatcher
    KernelHandle<ThreadDispatcher> handle;
    zx_rights_t thread_rights;
    result = ThreadDispatcher::Create(ktl::move(process), options, sp,
                                      &handle, &thread_rights);
    if (result != ZX_OK)
        return result;

    uint32_t tid = (uint32_t)handle.dispatcher()->get_koid();
    ktrace(TAG_THREAD_CREATE, tid, pid, 0, 0);
    ktrace_name(TAG_THREAD_NAME, tid, pid, buf);

    return out->make(ktl::move(handle), thread_rights);
}

// zx_status_t zx_thread_start
zx_status_t sys_thread_start(zx_handle_t handle, zx_vaddr_t thread_entry,
                             zx_vaddr_t stack, uintptr_t arg1, uintptr_t arg2) {
    LTRACEF("handle %x, entry %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg1 %#" PRIxPTR ", arg2 %#" PRIxPTR "\n",
            handle, thread_entry, stack, arg1, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ThreadDispatcher> thread;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_MANAGE_THREAD, &thread);
    if (status != ZX_OK) {
        return status;
    }

    ktrace(TAG_THREAD_START, (uint32_t)thread->get_koid(), 0, 0, 0);
    return thread->Start(thread_entry, stack, arg1, arg2, /* initial_thread= */ false);
}

void sys_thread_exit() {
    LTRACE_ENTRY;
    ThreadDispatcher::GetCurrent()->Exit();
}

// zx_status_t zx_thread_read_state
zx_status_t sys_thread_read_state(zx_handle_t handle, uint32_t kind,
                                  user_out_ptr<void> buffer, size_t buffer_size) {
    LTRACEF("handle %x, kind %u\n", handle, kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(ZX-968): debug rights
    fbl::RefPtr<ThreadDispatcher> thread;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &thread);
    if (status != ZX_OK)
        return status;

    thread_state_local_buffer_t local_buffer;
    size_t local_buffer_len = 0;
    status = validate_thread_state_input(kind, buffer_size, &local_buffer_len);
    if (status != ZX_OK)
        return status;

    status = thread->ReadState(static_cast<zx_thread_state_topic_t>(kind), &local_buffer,
                               local_buffer_len);
    if (status != ZX_OK)
        return status;

    if (buffer.copy_array_to_user(&local_buffer, local_buffer_len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;
    return ZX_OK;
}

// zx_status_t zx_thread_write_state
zx_status_t sys_thread_write_state(zx_handle_t handle, uint32_t kind,
                                   user_in_ptr<const void> buffer, size_t buffer_size) {
    LTRACEF("handle %x, kind %u\n", handle, kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(ZX-968): debug rights
    fbl::RefPtr<ThreadDispatcher> thread;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &thread);
    if (status != ZX_OK)
        return status;

    thread_state_local_buffer_t local_buffer;
    size_t local_buffer_len = 0;
    status = validate_thread_state_input(kind, buffer_size, &local_buffer_len);
    if (status != ZX_OK)
        return status;

    // Additionally check that the buffer is the exact size expected (validate only checks it's
    // larger, which is sufficient for reading).
    if (local_buffer_len != buffer_size)
        return ZX_ERR_INVALID_ARGS;

    status = buffer.copy_array_from_user(&local_buffer, local_buffer_len);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    return thread->WriteState(static_cast<zx_thread_state_topic_t>(kind), &local_buffer,
                              local_buffer_len);
}

// zx_status_t zx_task_suspend
zx_status_t sys_task_suspend(zx_handle_t handle, user_out_handle* token) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(ZX-858): Add support for jobs
    fbl::RefPtr<Dispatcher> task;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &task);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<SuspendTokenDispatcher> suspend_token;
    zx_rights_t rights;
    status = SuspendTokenDispatcher::Create(ktl::move(task), &suspend_token, &rights);

    if (status == ZX_OK)
        status = token->make(ktl::move(suspend_token), rights);

    return status;
}

// zx_status_t zx_task_suspend_token
zx_status_t sys_task_suspend_token(zx_handle_t handle, user_out_handle* token) {
    return sys_task_suspend(handle, token);
}

// zx_status_t zx_process_create
zx_status_t sys_process_create(zx_handle_t job_handle,
                               user_in_ptr<const char> _name, size_t name_len,
                               uint32_t options,
                               user_out_handle* proc_handle,
                               user_out_handle* vmar_handle) {
    LTRACEF("job handle %x, options %#x\n", job_handle, options);

    // currently, the only valid option value is 0
    if (options != 0)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    // We check the policy against the process calling zx_process_create, which
    // is the operative policy, rather than against |job_handle|. Access to
    // |job_handle| is controlled by the rights associated with the handle.
    zx_status_t result = up->QueryBasicPolicy(ZX_POL_NEW_PROCESS);
    if (result != ZX_OK)
        return result;

    // copy out the name
    char buf[ZX_MAX_NAME_LEN];
    fbl::StringPiece sp;
    // Silently truncate the given name.
    if (name_len > sizeof(buf))
        name_len = sizeof(buf);
    result = copy_user_string(_name, name_len, buf, sizeof(buf), &sp);
    if (result != ZX_OK)
        return result;
    LTRACEF("name %s\n", buf);

    fbl::RefPtr<JobDispatcher> job;
    auto status = up->GetDispatcherWithRights(job_handle, ZX_RIGHT_MANAGE_PROCESS, &job);
    if (status != ZX_OK) {
        // Try again, but with the WRITE right.
        // TODO(ZX-2967) Remove this when all callers are using MANAGE_PROCESS.
        status = up->GetDispatcherWithRights(job_handle, ZX_RIGHT_WRITE, &job);
        if (status != ZX_OK) {
            return status;
        }
    }

    // create a new process dispatcher
    KernelHandle<ProcessDispatcher> process_handle;
    fbl::RefPtr<VmAddressRegionDispatcher> vmar_dispatcher;
    zx_rights_t proc_rights, vmar_rights;
    result = ProcessDispatcher::Create(ktl::move(job), sp, options,
                                       &process_handle, &proc_rights,
                                       &vmar_dispatcher, &vmar_rights);
    if (result != ZX_OK)
        return result;

    uint32_t koid = (uint32_t)process_handle.dispatcher()->get_koid();
    ktrace(TAG_PROC_CREATE, koid, 0, 0, 0);
    ktrace_name(TAG_PROC_NAME, koid, 0, buf);

    // Give arch-specific tracing a chance to record process creation.
    arch_trace_process_create(koid, vmar_dispatcher->vmar()->aspace()->arch_aspace().arch_table_phys());

    result = proc_handle->make(ktl::move(process_handle), proc_rights);
    if (result == ZX_OK)
        result = vmar_handle->make(ktl::move(vmar_dispatcher), vmar_rights);
    return result;
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

// zx_status_t zx_process_start
zx_status_t sys_process_start(zx_handle_t process_handle, zx_handle_t thread_handle,
                              zx_vaddr_t pc, zx_vaddr_t sp,
                              zx_handle_t arg_handle_value, uintptr_t arg2) {
    LTRACEF("phandle %x, thandle %x, pc %#" PRIxPTR ", sp %#" PRIxPTR
            ", arg_handle %x, arg2 %#" PRIxPTR "\n",
            process_handle, thread_handle, pc, sp, arg_handle_value, arg2);

    auto up = ProcessDispatcher::GetCurrent();

    // get process dispatcher
    fbl::RefPtr<ProcessDispatcher> process;
    zx_status_t status = up->GetDispatcherWithRights(process_handle, ZX_RIGHT_WRITE, &process);
    if (status != ZX_OK) {
        up->RemoveHandle(arg_handle_value);
        return status;
    }

    // get thread_dispatcher
    fbl::RefPtr<ThreadDispatcher> thread;
    status = up->GetDispatcherWithRights(thread_handle, ZX_RIGHT_WRITE, &thread);
    if (status != ZX_OK) {
        up->RemoveHandle(arg_handle_value);
        return status;
    }

    HandleOwner arg_handle = up->RemoveHandle(arg_handle_value);

    // test that the thread belongs to the starting process
    if (thread->process() != process.get())
        return ZX_ERR_ACCESS_DENIED;

    zx_handle_t arg_nhv = ZX_HANDLE_INVALID;
    if (arg_handle) {
        if (!arg_handle->HasRights(ZX_RIGHT_TRANSFER))
            return ZX_ERR_ACCESS_DENIED;
        arg_nhv = process->MapHandleToValue(arg_handle);
        process->AddHandle(ktl::move(arg_handle));
    }

    status = thread->Start(pc, sp, static_cast<uintptr_t>(arg_nhv),
                           arg2, /* initial_thread */ true);
    if (status != ZX_OK) {
        // Remove |arg_handle| from the process that failed to start.
        process->RemoveHandle(arg_nhv);
        return status;
    }

    ktrace(TAG_PROC_START, (uint32_t)thread->get_koid(),
           (uint32_t)process->get_koid(), 0, 0);

    return ZX_OK;
}

void sys_process_exit(int64_t retcode) {
    LTRACEF("retcode %" PRId64 "\n", retcode);
    ProcessDispatcher::GetCurrent()->Exit(retcode);
}

// zx_status_t zx_process_read_memory
zx_status_t sys_process_read_memory(zx_handle_t handle, zx_vaddr_t vaddr,
                                    user_out_ptr<void> buffer,
                                    size_t buffer_size, user_out_ptr<size_t> _actual) {
    LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, buffer_size);

    if (!buffer)
        return ZX_ERR_INVALID_ARGS;
    if (buffer_size == 0 || buffer_size > kMaxDebugReadBlock)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                                                     &process);
    if (status != ZX_OK)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return ZX_ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ZX_ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return ZX_ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return ZX_ERR_NO_MEMORY;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(ZX-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = buffer.reinterpret<uint8_t>();
        for (size_t i = 0; i < buffer_size; i += PAGE_SIZE) {
            status = int_data.copy_array_to_user(&byte, 1, i);
            if (status != ZX_OK) {
                return status;
            }
        }
        if (buffer_size > 0) {
            status = int_data.copy_array_to_user(&byte, 1, buffer_size - 1);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    // TODO(ZX-1631): While this limits reading to the mapped address space of
    // this VMO, it should be reading from multiple VMOs, not a single one.
    // Additionally, it is racy with the mapping going away.
    buffer_size = MIN(buffer_size, vm_mapping->size() - (vaddr - vm_mapping->base()));
    zx_status_t st = vmo->ReadUser(buffer, offset, buffer_size);

    if (st == ZX_OK) {
        zx_status_t status = _actual.copy_to_user(static_cast<size_t>(buffer_size));
        if (status != ZX_OK)
            return status;
    }
    return st;
}

// zx_status_t zx_process_write_memory
zx_status_t sys_process_write_memory(zx_handle_t handle, zx_vaddr_t vaddr,
                                     user_in_ptr<const void> buffer,
                                     size_t buffer_size, user_out_ptr<size_t> _actual) {
    LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, buffer_size);

    if (!buffer)
        return ZX_ERR_INVALID_ARGS;
    if (buffer_size == 0 || buffer_size > kMaxDebugWriteBlock)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ProcessDispatcher> process;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &process);
    if (status != ZX_OK)
        return status;

    auto aspace = process->aspace();
    if (!aspace)
        return ZX_ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ZX_ERR_NO_MEMORY;

    auto vm_mapping = region->as_vm_mapping();
    if (!vm_mapping)
        return ZX_ERR_NO_MEMORY;

    auto vmo = vm_mapping->vmo();
    if (!vmo)
        return ZX_ERR_NO_MEMORY;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(ZX-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = buffer.reinterpret<const uint8_t>();
        for (size_t i = 0; i < buffer_size; i += PAGE_SIZE) {
            status = int_data.copy_array_from_user(&byte, 1, i);
            if (status != ZX_OK) {
                return status;
            }
        }
        if (buffer_size > 0) {
            status = int_data.copy_array_from_user(&byte, 1, buffer_size - 1);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
    // TODO(ZX-1631): While this limits writing to the mapped address space of
    // this VMO, it should be writing to multiple VMOs, not a single one.
    // Additionally, it is racy with the mapping going away.
    buffer_size = MIN(buffer_size, vm_mapping->size() - (vaddr - vm_mapping->base()));
    zx_status_t st = vmo->WriteUser(buffer, offset, buffer_size);

    if (st == ZX_OK) {
        zx_status_t status = _actual.copy_to_user(static_cast<size_t>(buffer_size));
        if (status != ZX_OK)
            return status;
    }
    return st;
}

// helper routine for sys_task_kill
template <typename T, bool retcode>
static zx_status_t kill_task(fbl::RefPtr<Dispatcher> dispatcher) {
    auto task = DownCastDispatcher<T>(&dispatcher);
    if (!task)
        return ZX_ERR_WRONG_TYPE;

    if constexpr (retcode) {
        task->Kill(ZX_TASK_RETCODE_SYSCALL_KILL);
    } else {
        task->Kill();
    }
    return ZX_OK;
}

// zx_status_t zx_task_kill
zx_status_t sys_task_kill(zx_handle_t task_handle) {
    LTRACEF("handle %x\n", task_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcherWithRights(task_handle, ZX_RIGHT_DESTROY, &dispatcher);
    if (status != ZX_OK)
        return status;

    // see if it's a process or thread and dispatch accordingly
    switch (dispatcher->get_type()) {
    case ZX_OBJ_TYPE_JOB:
        return kill_task<JobDispatcher, true>(ktl::move(dispatcher));
    case ZX_OBJ_TYPE_PROCESS:
        return kill_task<ProcessDispatcher, true>(ktl::move(dispatcher));
    case ZX_OBJ_TYPE_THREAD:
        return kill_task<ThreadDispatcher, false>(ktl::move(dispatcher));
    default:
        return ZX_ERR_WRONG_TYPE;
    }
}

// zx_status_t zx_job_create
zx_status_t sys_job_create(zx_handle_t parent_job, uint32_t options,
                           user_out_handle* out) {
    LTRACEF("parent: %x\n", parent_job);

    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> parent;
    zx_status_t status = up->GetDispatcherWithRights(parent_job, ZX_RIGHT_MANAGE_JOB, &parent);
    if (status != ZX_OK) {
        // Try again, but with the WRITE right.
        // TODO(kulakowski) Remove this when all callers are using MANAGE_JOB.
        status = up->GetDispatcherWithRights(parent_job, ZX_RIGHT_WRITE, &parent);
        if (status != ZX_OK) {
            return status;
        }
    }

    KernelHandle<JobDispatcher> handle;
    zx_rights_t rights;
    status = JobDispatcher::Create(options, ktl::move(parent), &handle, &rights);
    if (status == ZX_OK)
        status = out->make(ktl::move(handle), rights);
    return status;
}

static zx_status_t job_set_policy_basic(zx_handle_t handle, uint32_t options,
                                        user_in_ptr<const void> _policy, uint32_t count) {
    if ((options != ZX_JOB_POL_RELATIVE) && (options != ZX_JOB_POL_ABSOLUTE)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!_policy || (count == 0u)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    fbl::InlineArray<
        zx_policy_basic, kPolicyBasicInlineCount>
        policy(&ac, count);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = _policy.copy_array_from_user(policy.get(), sizeof(zx_policy_basic) * count);
    if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> job;
    status = up->GetDispatcherWithRights(handle, ZX_RIGHT_SET_POLICY, &job);
    if (status != ZX_OK) {
        return status;
    }

    return job->SetBasicPolicy(options, policy.get(), policy.size());
}

static zx_status_t job_set_policy_timer_slack(zx_handle_t handle, uint32_t options,
                                              user_in_ptr<const void> _policy, uint32_t count) {
    if (options != ZX_JOB_POL_RELATIVE) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!_policy || (count != 1u)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_policy_timer_slack slack_policy;
    auto status = _policy.reinterpret<const zx_policy_timer_slack>().copy_from_user(&slack_policy);
    if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<JobDispatcher> job;
    status = up->GetDispatcherWithRights(handle, ZX_RIGHT_SET_POLICY, &job);
    if (status != ZX_OK) {
        return status;
    }

    return job->SetTimerSlackPolicy(slack_policy);
}

// zx_status_t zx_job_set_policy
zx_status_t sys_job_set_policy(zx_handle_t handle, uint32_t options,
                               uint32_t topic, user_in_ptr<const void> _policy,
                               uint32_t count) {
    switch (topic) {
    case ZX_JOB_POL_BASIC:
        return job_set_policy_basic(handle, options, _policy, count);
    case ZX_JOB_POL_TIMER_SLACK:
        return job_set_policy_timer_slack(handle, options, _policy, count);
    default:
        return ZX_ERR_INVALID_ARGS;
    };
}
