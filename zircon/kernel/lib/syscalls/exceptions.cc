// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/syscalls/forward.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <object/exception_dispatcher.h>
#include <object/exceptionate.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

// zx_status_t zx_task_create_exception_channel
zx_status_t sys_task_create_exception_channel(zx_handle_t handle, uint32_t options,
                                              user_out_handle* out) {
  LTRACE_ENTRY;

  if (options & ~ZX_EXCEPTION_CHANNEL_DEBUGGER)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->EnforceBasicPolicy(ZX_POL_NEW_CHANNEL);
  if (status != ZX_OK)
    return status;

  // Required rights to receive exceptions:
  //   INSPECT: provides non-trivial task information
  //   DUPLICATE: can create new thread and process handles
  //   TRANSFER: exceptions or their channels can be transferred
  //   MANAGE_THREAD: can keep thread paused during exception
  //   ENUMERATE (job/process): can access child thread (enforced below)
  //
  // In the future we may want to support some smarter behavior here e.g.
  // allowing for exceptions but no task handles if these rights don't exist,
  // but to start with we'll keep it simple until we know we want this.
  fbl::RefPtr<Dispatcher> task;
  zx_rights_t task_rights;
  status = up->handle_table().GetDispatcherWithRights(
      *up, handle,
      ZX_RIGHT_INSPECT | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_MANAGE_THREAD, &task,
      &task_rights);
  if (status != ZX_OK)
    return status;

  // The task handles provided over this exception channel use the rights on
  // |handle| so we are sure not to grant any additional rights the caller
  // didn't already have.
  //
  // TODO(fxbug.dev/33015): thread/process/job rights don't always map 1:1.
  zx_rights_t process_rights = task_rights;
  zx_rights_t thread_rights = task_rights;

  // Only one of the |exceptionate| and |job_to_create_debug_exceptionate| will be non-null.
  //
  // When creating debugger exception channel on a job, |exceptionate| will be null and
  // |job_to_create_debug_exceptionate| is used instead, because JobDispatcher::DebugExceptionate
  // requires dynamic allocation. For other types, only |exceptionate| will be used.
  Exceptionate* exceptionate = nullptr;
  JobDispatcher* job_to_create_debug_exceptionate = nullptr;

  bool job_or_process = false;

  // Use DownCastDispatcher() on the raw task pointer to avoid moving the
  // RefPtr out, we still need to retain the RefPtr to keep our extracted
  // Exceptionate* alive.
  if (auto job = DownCastDispatcher<JobDispatcher>(task.get())) {
    if (options & ZX_EXCEPTION_CHANNEL_DEBUGGER) {
      job_to_create_debug_exceptionate = job;
    } else {
      exceptionate = job->exceptionate();
    }
    job_or_process = true;
  } else if (auto process = DownCastDispatcher<ProcessDispatcher>(task.get())) {
    if (options & ZX_EXCEPTION_CHANNEL_DEBUGGER) {
      exceptionate = process->debug_exceptionate();
    } else {
      exceptionate = process->exceptionate();
    }
    job_or_process = true;
  } else if (auto thread = DownCastDispatcher<ThreadDispatcher>(task.get())) {
    if (options & ZX_EXCEPTION_CHANNEL_DEBUGGER)
      return ZX_ERR_INVALID_ARGS;

    // We don't provide access up the task chain, so don't send the process
    // handle when we're registering on a thread.
    process_rights = 0;
    exceptionate = thread->exceptionate();
  } else {
    return ZX_ERR_WRONG_TYPE;
  }

  DEBUG_ASSERT(task);

  // For job and process handlers, we require the handle be able to enumerate
  // as proof that the caller is allowed to get to the thread handle.
  if (job_or_process && !(task_rights & ZX_RIGHT_ENUMERATE))
    return ZX_ERR_ACCESS_DENIED;

  KernelHandle<ChannelDispatcher> kernel_handle, user_handle;
  zx_rights_t rights;
  status = ChannelDispatcher::Create(&kernel_handle, &user_handle, &rights);
  if (status != ZX_OK)
    return status;

  if (job_to_create_debug_exceptionate) {
    status = job_to_create_debug_exceptionate->CreateDebugExceptionate(
        ktl::move(kernel_handle), thread_rights, process_rights);
  } else {
    status = exceptionate->SetChannel(ktl::move(kernel_handle), thread_rights, process_rights);
  }
  if (status != ZX_OK)
    return status;

  // Strip unwanted rights from the user endpoint, exception channels are
  // read-only from userspace.
  //
  // We don't need to remove the task channel if this fails. Exception
  // channels are built to handle the userspace peer closing so it will just
  // follow that path if we fail to copy the userspace endpoint out.
  return out->make(ktl::move(user_handle),
                   rights & (ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_READ));
}

// zx_status_t zx_exception_get_thread
zx_status_t sys_exception_get_thread(zx_handle_t handle, user_out_handle* thread) {
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ExceptionDispatcher> exception;
  zx_status_t status = up->handle_table().GetDispatcher(*up, handle, &exception);
  if (status != ZX_OK) {
    return status;
  }

  HandleOwner thread_handle;
  status = exception->MakeThreadHandle(&thread_handle);
  if (status != ZX_OK) {
    return status;
  }

  return thread->transfer(ktl::move(thread_handle));
}

// zx_status_t zx_exception_get_process
zx_status_t sys_exception_get_process(zx_handle_t handle, user_out_handle* process) {
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<ExceptionDispatcher> exception;
  zx_status_t status = up->handle_table().GetDispatcher(*up, handle, &exception);
  if (status != ZX_OK) {
    return status;
  }

  HandleOwner process_handle;
  status = exception->MakeProcessHandle(&process_handle);
  if (status != ZX_OK) {
    return status;
  }

  return process->transfer(ktl::move(process_handle));
}
