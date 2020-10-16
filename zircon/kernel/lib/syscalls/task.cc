// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/userabi/vdso.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/policy.h>

#include <arch/arch_ops.h>
#include <fbl/auto_lock.h>
#include <fbl/inline_array.h>
#include <fbl/ref_ptr.h>
#include <ktl/algorithm.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/suspend_token_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

namespace {

constexpr size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;
constexpr size_t kMaxDebugWriteBlock = 64 * 1024u * 1024u;

// TODO(fxbug.dev/30969): copy_user_string may truncate the incoming string,
// and may copy extra data past the NUL.
// TODO(dbort): If anyone else needs this, move it into user_ptr.
zx_status_t copy_user_string(const user_in_ptr<const char>& src, size_t src_len, char* buf,
                             size_t buf_len, ktl::string_view* sp) {
  // Disallow 0 buf_len (since we are copying into it), but allow 0 src_len (to allow
  // "", src_len doesn't include '\0'). With the check for buf_len, we won't underflow
  // src_len below. Also, 0 src_len is valid input (for "" src strings).
  if (!src || buf_len == 0 || src_len > buf_len) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t result = src.copy_array_from_user(buf, src_len);
  if (result != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }

  // ensure zero termination
  size_t str_len = (src_len == buf_len ? src_len - 1 : src_len);
  buf[str_len] = 0;
  *sp = ktl::string_view(buf);

  return ZX_OK;
}

}  // namespace

// zx_status_t zx_thread_create
zx_status_t sys_thread_create(zx_handle_t process_handle, user_in_ptr<const char> _name,
                              size_t name_len, uint32_t options, user_out_handle* out) {
  LTRACEF("process handle %x, options %#x\n", process_handle, options);

  // currently, the only valid option value is 0
  if (options != 0)
    return ZX_ERR_INVALID_ARGS;

  // copy out the name
  char buf[ZX_MAX_NAME_LEN];
  ktl::string_view sp;
  // Silently truncate the given name.
  if (name_len > sizeof(buf))
    name_len = sizeof(buf);
  zx_status_t result = copy_user_string(_name, name_len, buf, sizeof(buf), &sp);
  if (result != ZX_OK)
    return result;
  LTRACEF("name %s\n", buf);

  // convert process handle to process dispatcher
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ProcessDispatcher> process;
  result =
      up->handle_table().GetDispatcherWithRights(process_handle, ZX_RIGHT_MANAGE_THREAD, &process);
  if (result != ZX_OK) {
    return result;
  }

  auto pid = static_cast<uint32_t>(process->get_koid());

  // create the thread dispatcher
  KernelHandle<ThreadDispatcher> handle;
  zx_rights_t thread_rights;
  result = ThreadDispatcher::Create(ktl::move(process), options, sp, &handle, &thread_rights);
  if (result != ZX_OK)
    return result;

  result = handle.dispatcher()->Initialize();
  if (result != ZX_OK) {
    return result;
  }

  auto tid = static_cast<uint32_t>(handle.dispatcher()->get_koid());

  ktrace(TAG_THREAD_CREATE, tid, pid, 0, 0);
  ktrace_name(TAG_THREAD_NAME, tid, pid, buf);

  return out->make(ktl::move(handle), thread_rights);
}

// zx_status_t zx_thread_start
zx_status_t sys_thread_start(zx_handle_t handle, zx_vaddr_t thread_entry, zx_vaddr_t stack,
                             uintptr_t arg1, uintptr_t arg2) {
  LTRACEF("handle %x, entry %#" PRIxPTR ", sp %#" PRIxPTR ", arg1 %#" PRIxPTR ", arg2 %#" PRIxPTR
          "\n",
          handle, thread_entry, stack, arg1, arg2);
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ThreadDispatcher> thread;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_MANAGE_THREAD, &thread);
  if (status != ZX_OK) {
    return status;
  }

  ktrace(TAG_THREAD_START, (uint32_t)thread->get_koid(), 0, 0, 0);
  return thread->Start(ThreadDispatcher::EntryState{thread_entry, stack, arg1, arg2},
                       /* initial_thread= */ false);
}

void sys_thread_exit() {
  LTRACE_ENTRY;
  ThreadDispatcher::ExitCurrent();
}

// zx_status_t zx_thread_read_state
zx_status_t sys_thread_read_state(zx_handle_t handle, uint32_t kind, user_out_ptr<void> buffer,
                                  size_t buffer_size) {
  LTRACEF("handle %x, kind %u\n", handle, kind);

  auto up = ProcessDispatcher::GetCurrent();

  // TODO(fxbug.dev/30915): debug rights
  fbl::RefPtr<ThreadDispatcher> thread;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ, &thread);
  if (status != ZX_OK)
    return status;

  return thread->ReadState(static_cast<zx_thread_state_topic_t>(kind), buffer, buffer_size);
}

// zx_status_t zx_thread_write_state
zx_status_t sys_thread_write_state(zx_handle_t handle, uint32_t kind,
                                   user_in_ptr<const void> buffer, size_t buffer_size) {
  LTRACEF("handle %x, kind %u\n", handle, kind);

  auto up = ProcessDispatcher::GetCurrent();

  // TODO(fxbug.dev/30915): debug rights
  fbl::RefPtr<ThreadDispatcher> thread;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &thread);
  if (status != ZX_OK)
    return status;

  return thread->WriteState(static_cast<zx_thread_state_topic_t>(kind), buffer, buffer_size);
}

// zx_status_t zx_task_suspend
zx_status_t sys_task_suspend(zx_handle_t handle, user_out_handle* token) {
  LTRACE_ENTRY;

  auto up = ProcessDispatcher::GetCurrent();

  // TODO(fxbug.dev/30807): Add support for jobs
  fbl::RefPtr<Dispatcher> task;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &task);
  if (status != ZX_OK)
    return status;

  KernelHandle<SuspendTokenDispatcher> new_token;
  zx_rights_t rights;
  status = SuspendTokenDispatcher::Create(ktl::move(task), &new_token, &rights);

  if (status == ZX_OK)
    status = token->make(ktl::move(new_token), rights);

  return status;
}

// zx_status_t zx_task_suspend_token
zx_status_t sys_task_suspend_token(zx_handle_t handle, user_out_handle* token) {
  return sys_task_suspend(handle, token);
}

// zx_status_t zx_process_create
zx_status_t sys_process_create(zx_handle_t job_handle, user_in_ptr<const char> _name,
                               size_t name_len, uint32_t options, user_out_handle* proc_handle,
                               user_out_handle* vmar_handle) {
  LTRACEF("job handle %x, options %#x\n", job_handle, options);

  // currently, the only valid option value is 0
  if (options != 0)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  // We check the policy against the process calling zx_process_create, which
  // is the operative policy, rather than against |job_handle|. Access to
  // |job_handle| is controlled by the rights associated with the handle.
  zx_status_t result = up->EnforceBasicPolicy(ZX_POL_NEW_PROCESS);
  if (result != ZX_OK)
    return result;

  // copy out the name
  char buf[ZX_MAX_NAME_LEN];
  ktl::string_view sp;
  // Silently truncate the given name.
  if (name_len > sizeof(buf))
    name_len = sizeof(buf);
  result = copy_user_string(_name, name_len, buf, sizeof(buf), &sp);
  if (result != ZX_OK)
    return result;
  LTRACEF("name %s\n", buf);

  fbl::RefPtr<JobDispatcher> job;
  auto status =
      up->handle_table().GetDispatcherWithRights(job_handle, ZX_RIGHT_MANAGE_PROCESS, &job);
  if (status != ZX_OK) {
    // Try again, but with the WRITE right.
    // TODO(fxbug.dev/32803) Remove this when all callers are using MANAGE_PROCESS.
    status = up->handle_table().GetDispatcherWithRights(job_handle, ZX_RIGHT_WRITE, &job);
    if (status != ZX_OK) {
      return status;
    }
  }

  // create a new process dispatcher
  KernelHandle<ProcessDispatcher> new_process_handle;
  KernelHandle<VmAddressRegionDispatcher> new_vmar_handle;
  zx_rights_t proc_rights, vmar_rights;
  result = ProcessDispatcher::Create(ktl::move(job), sp, options, &new_process_handle, &proc_rights,
                                     &new_vmar_handle, &vmar_rights);
  if (result != ZX_OK)
    return result;

  uint32_t koid = (uint32_t)new_process_handle.dispatcher()->get_koid();
  ktrace(TAG_PROC_CREATE, koid, 0, 0, 0);
  ktrace_name(TAG_PROC_NAME, koid, 0, buf);

  // Give arch-specific tracing a chance to record process creation.
  arch_trace_process_create(
      koid, new_vmar_handle.dispatcher()->vmar()->aspace()->arch_aspace().arch_table_phys());

  result = proc_handle->make(ktl::move(new_process_handle), proc_rights);
  if (result == ZX_OK)
    result = vmar_handle->make(ktl::move(new_vmar_handle), vmar_rights);
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
zx_status_t sys_process_start(zx_handle_t process_handle, zx_handle_t thread_handle, zx_vaddr_t pc,
                              zx_vaddr_t sp, zx_handle_t arg_handle_value, uintptr_t arg2) {
  LTRACEF("phandle %x, thandle %x, pc %#" PRIxPTR ", sp %#" PRIxPTR
          ", arg_handle %x, arg2 %#" PRIxPTR "\n",
          process_handle, thread_handle, pc, sp, arg_handle_value, arg2);
  auto up = ProcessDispatcher::GetCurrent();

  // get process dispatcher
  fbl::RefPtr<ProcessDispatcher> process;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(process_handle, ZX_RIGHT_WRITE, &process);
  if (status != ZX_OK) {
    up->handle_table().RemoveHandle(arg_handle_value);
    return status;
  }

  // get thread_dispatcher
  fbl::RefPtr<ThreadDispatcher> thread;
  status = up->handle_table().GetDispatcherWithRights(thread_handle, ZX_RIGHT_WRITE, &thread);
  if (status != ZX_OK) {
    up->handle_table().RemoveHandle(arg_handle_value);
    return status;
  }

  HandleOwner arg_handle = up->handle_table().RemoveHandle(arg_handle_value);

  // test that the thread belongs to the starting process
  if (thread->process() != process.get())
    return ZX_ERR_ACCESS_DENIED;

  zx_handle_t arg_nhv = ZX_HANDLE_INVALID;
  if (arg_handle) {
    if (!arg_handle->HasRights(ZX_RIGHT_TRANSFER))
      return ZX_ERR_ACCESS_DENIED;
    arg_nhv = process->handle_table().MapHandleToValue(arg_handle);
    process->handle_table().AddHandle(ktl::move(arg_handle));
  }

  status =
      thread->Start(ThreadDispatcher::EntryState{pc, sp, static_cast<uintptr_t>(arg_nhv), arg2},
                    /* initial_thread */ true);
  if (status != ZX_OK) {
    // Remove |arg_handle| from the process that failed to start.
    process->handle_table().RemoveHandle(arg_nhv);
    return status;
  }

  ktrace(TAG_PROC_START, (uint32_t)thread->get_koid(), (uint32_t)process->get_koid(), 0, 0);

  return ZX_OK;
}

void sys_process_exit(int64_t retcode) {
  LTRACEF("retcode %" PRId64 "\n", retcode);
  ProcessDispatcher::ExitCurrent(retcode);
}

// zx_status_t zx_process_read_memory
zx_status_t sys_process_read_memory(zx_handle_t handle, zx_vaddr_t vaddr, user_out_ptr<void> buffer,
                                    size_t buffer_size, user_out_ptr<size_t> _actual) {
  LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, buffer_size);

  if (!buffer)
    return ZX_ERR_INVALID_ARGS;
  if (buffer_size == 0 || buffer_size > kMaxDebugReadBlock)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ProcessDispatcher> process;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE, &process);
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

  uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
  // TODO(fxbug.dev/31512): While this limits reading to the mapped address space of
  // this VMO, it should be reading from multiple VMOs, not a single one.
  // Additionally, it is racy with the mapping going away.
  buffer_size = ktl::min(buffer_size, vm_mapping->size() - (vaddr - vm_mapping->base()));
  zx_status_t st =
      vmo->ReadUser(up->aspace().get(), buffer.reinterpret<char>(), offset, buffer_size);

  if (st == ZX_OK) {
    zx_status_t status = _actual.copy_to_user(static_cast<size_t>(buffer_size));
    if (status != ZX_OK)
      return status;
  }
  return st;
}

// zx_status_t zx_process_write_memory
zx_status_t sys_process_write_memory(zx_handle_t handle, zx_vaddr_t vaddr,
                                     user_in_ptr<const void> buffer, size_t buffer_size,
                                     user_out_ptr<size_t> _actual) {
  LTRACEF("vaddr 0x%" PRIxPTR ", size %zu\n", vaddr, buffer_size);

  if (!DebuggingSyscallsEnabled()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!buffer)
    return ZX_ERR_INVALID_ARGS;
  if (buffer_size == 0 || buffer_size > kMaxDebugWriteBlock)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ProcessDispatcher> process;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &process);
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

  if (VDso::vmo_is_vdso(vmo)) {
    // Don't allow writes to the vDSO.
    return ZX_ERR_ACCESS_DENIED;
  }

  uint64_t offset = vaddr - vm_mapping->base() + vm_mapping->object_offset();
  // TODO(fxbug.dev/31512): While this limits writing to the mapped address space of
  // this VMO, it should be writing to multiple VMOs, not a single one.
  // Additionally, it is racy with the mapping going away.
  buffer_size = ktl::min(buffer_size, vm_mapping->size() - (vaddr - vm_mapping->base()));
  zx_status_t st =
      vmo->WriteUser(up->aspace().get(), buffer.reinterpret<const char>(), offset, buffer_size);

  if (st == ZX_OK) {
    zx_status_t status = _actual.copy_to_user(static_cast<size_t>(buffer_size));
    if (status != ZX_OK)
      return status;
  }
  return st;
}

// helper routine for sys_task_kill
template <typename T>
static zx_status_t kill_task(fbl::RefPtr<Dispatcher> dispatcher) {
  auto task = DownCastDispatcher<T>(&dispatcher);
  if (!task)
    return ZX_ERR_WRONG_TYPE;

  task->Kill(ZX_TASK_RETCODE_SYSCALL_KILL);
  return ZX_OK;
}

// zx_status_t zx_task_kill
zx_status_t sys_task_kill(zx_handle_t task_handle) {
  LTRACEF("handle %x\n", task_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<Dispatcher> dispatcher;
  auto status =
      up->handle_table().GetDispatcherWithRights(task_handle, ZX_RIGHT_DESTROY, &dispatcher);
  if (status != ZX_OK)
    return status;

  // See if it's a process or job and dispatch accordingly. Killing a thread is not supported.
  switch (dispatcher->get_type()) {
    case ZX_OBJ_TYPE_JOB:
      return kill_task<JobDispatcher>(ktl::move(dispatcher));
    case ZX_OBJ_TYPE_PROCESS:
      return kill_task<ProcessDispatcher>(ktl::move(dispatcher));
    default:
      return ZX_ERR_WRONG_TYPE;
  }
}

// zx_status_t zx_job_create
zx_status_t sys_job_create(zx_handle_t parent_job, uint32_t options, user_out_handle* out) {
  LTRACEF("parent: %x\n", parent_job);

  if (options != 0u)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<JobDispatcher> parent;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(parent_job, ZX_RIGHT_MANAGE_JOB, &parent);
  if (status != ZX_OK) {
    // Try again, but with the WRITE right.
    // TODO(kulakowski) Remove this when all callers are using MANAGE_JOB.
    status = up->handle_table().GetDispatcherWithRights(parent_job, ZX_RIGHT_WRITE, &parent);
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

template <typename TPolicy>
static zx_status_t job_set_policy_basic(zx_handle_t handle, uint32_t options,
                                        user_in_ptr<const void> _policy, uint32_t count) {
  if ((options != ZX_JOB_POL_RELATIVE) && (options != ZX_JOB_POL_ABSOLUTE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!_policy || (count == 0u) || (count > 32u)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  fbl::InlineArray<TPolicy, kPolicyBasicInlineCount> policy(&ac, count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = _policy.reinterpret<const TPolicy>().copy_array_from_user(policy.get(), count);
  if (status != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<JobDispatcher> job;
  status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_SET_POLICY, &job);
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
  status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_SET_POLICY, &job);
  if (status != ZX_OK) {
    return status;
  }

  return job->SetTimerSlackPolicy(slack_policy);
}

// zx_status_t zx_job_set_policy
zx_status_t sys_job_set_policy(zx_handle_t handle, uint32_t options, uint32_t topic,
                               user_in_ptr<const void> _policy, uint32_t count) {
  switch (topic) {
    case ZX_JOB_POL_BASIC_V1:
      return job_set_policy_basic<zx_policy_basic_v1>(handle, options, _policy, count);
    case ZX_JOB_POL_BASIC_V2:
      return job_set_policy_basic<zx_policy_basic_v2>(handle, options, _policy, count);
    case ZX_JOB_POL_TIMER_SLACK:
      return job_set_policy_timer_slack(handle, options, _policy, count);
    default:
      return ZX_ERR_INVALID_ARGS;
  };
}

zx_status_t sys_job_set_critical(zx_handle_t job_handle, uint32_t options,
                                 zx_handle_t process_handle) {
  bool retcode_nonzero = false;
  if (options == ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO) {
    retcode_nonzero = true;
  } else if (options != 0u) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<JobDispatcher> job;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(job_handle, ZX_RIGHT_DESTROY, &job);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<ProcessDispatcher> process;
  status = up->handle_table().GetDispatcherWithRights(process_handle, ZX_RIGHT_WAIT, &process);
  if (status != ZX_OK) {
    return status;
  }

  return process->SetCriticalToJob(ktl::move(job), retcode_nonzero);
}
