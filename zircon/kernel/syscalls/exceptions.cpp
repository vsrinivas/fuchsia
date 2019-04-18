// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <object/exceptionate.h>
#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

static zx_status_t object_unbind_exception_port(zx_handle_t obj_handle, bool debugger) {
    // TODO(ZX-968): check rights once appropriate right is determined
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        return job->ResetExceptionPort(debugger) ? ZX_OK : ZX_ERR_BAD_STATE; // No port was bound.
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        return process->ResetExceptionPort(debugger) ? ZX_OK
                                                     : ZX_ERR_BAD_STATE; // No port was bound.
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return ZX_ERR_INVALID_ARGS;
        return thread->ResetExceptionPort() ? ZX_OK : ZX_ERR_BAD_STATE; // No port was bound.
    }

    return ZX_ERR_WRONG_TYPE;
}

static zx_status_t task_bind_exception_port(zx_handle_t obj_handle, zx_handle_t eport_handle, uint64_t key, bool debugger) {
    // TODO(ZX-968): check rights once appropriate right is determined
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PortDispatcher> port;
    zx_status_t status = up->GetDispatcher(eport_handle, &port);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<ExceptionPort> eport;

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        ExceptionPort::Type type;
        if (debugger)
            type = ExceptionPort::Type::JOB_DEBUGGER;
        else
            type = ExceptionPort::Type::JOB;
        status = ExceptionPort::Create(type,
                                       ktl::move(port), key, &eport);
        if (status != ZX_OK)
            return status;
        status = job->SetExceptionPort(eport);
        if (status != ZX_OK)
            return status;

        eport->SetTarget(job);
        return ZX_OK;
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        ExceptionPort::Type type;
        if (debugger)
            type = ExceptionPort::Type::DEBUGGER;
        else
            type = ExceptionPort::Type::PROCESS;
        status = ExceptionPort::Create(type, ktl::move(port), key, &eport);
        if (status != ZX_OK)
            return status;
        status = process->SetExceptionPort(eport);
        if (status != ZX_OK)
            return status;

        eport->SetTarget(process);
        return ZX_OK;
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return ZX_ERR_INVALID_ARGS;
        status = ExceptionPort::Create(ExceptionPort::Type::THREAD,
                                       ktl::move(port), key, &eport);
        if (status != ZX_OK)
            return status;
        status = thread->SetExceptionPort(eport);
        if (status != ZX_OK)
            return status;

        eport->SetTarget(thread);
        return ZX_OK;
    }

    return ZX_ERR_WRONG_TYPE;
}

// zx_status_t zx_task_bind_exception_port
zx_status_t sys_task_bind_exception_port(zx_handle_t handle, zx_handle_t port,
                                           uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (options & ~ZX_EXCEPTION_PORT_DEBUGGER)
        return ZX_ERR_INVALID_ARGS;

    bool debugger = (options & ZX_EXCEPTION_PORT_DEBUGGER) != 0;

    if (port == ZX_HANDLE_INVALID) {
        return object_unbind_exception_port(handle, debugger);
    } else {
        return task_bind_exception_port(handle, port, key, debugger);
    }
}

// zx_status_t zx_task_resume_from_exception
zx_status_t sys_task_resume_from_exception(zx_handle_t handle, zx_handle_t port, uint32_t options) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ThreadDispatcher> thread;
    zx_status_t status = up->GetDispatcher(handle, &thread);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<PortDispatcher> eport;
    status = up->GetDispatcher(port, &eport);
    if (status != ZX_OK)
        return status;

    // Currently the only option is the ZX_RESUME_TRY_NEXT bit.
    if (options != 0 && options != ZX_RESUME_TRY_NEXT)
        return ZX_ERR_INVALID_ARGS;

    if (options & ZX_RESUME_TRY_NEXT) {
        return thread->MarkExceptionNotHandled(eport.get());
    } else {
        return thread->MarkExceptionHandled(eport.get());
    }
}

// zx_status_t zx_task_create_exception_channel
zx_status_t sys_task_create_exception_channel(zx_handle_t handle, uint32_t options,
                                              user_out_handle* out) {
    LTRACE_ENTRY;

    if (options & ~ZX_EXCEPTION_PORT_DEBUGGER)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t status = up->QueryBasicPolicy(ZX_POL_NEW_CHANNEL);
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
    status = up->GetDispatcherWithRights(
        handle, ZX_RIGHT_INSPECT | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_MANAGE_THREAD,
        &task, &task_rights);
    if (status != ZX_OK)
        return status;

    // The task handles provided over this exception channel use the rights on
    // |handle| so we are sure not to grant any additional rights the caller
    // didn't already have.
    //
    // TODO(ZX-3208): thread/process/job rights don't always map 1:1.
    zx_rights_t process_rights = task_rights;
    zx_rights_t thread_rights = task_rights;

    Exceptionate::Type type =
        (options & ZX_EXCEPTION_PORT_DEBUGGER) ? Exceptionate::Type::kDebug
                                               : Exceptionate::Type::kStandard;
    Exceptionate* exceptionate = nullptr;
    bool job_or_process = false;

    // Use DownCastDispatcher() on the raw task pointer to avoid moving the
    // RefPtr out, we still need to retain the RefPtr to keep our extracted
    // Exceptionate* alive.
    if (auto job = DownCastDispatcher<JobDispatcher>(task.get())) {
        exceptionate = job->exceptionate(type);
        job_or_process = true;
    } else if (auto process = DownCastDispatcher<ProcessDispatcher>(task.get())) {
        exceptionate = process->exceptionate(type);
        job_or_process = true;
    } else if (auto thread = DownCastDispatcher<ThreadDispatcher>(task.get())) {
        if (type == Exceptionate::Type::kDebug)
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

    fbl::RefPtr<ChannelDispatcher> kernel_channel, user_channel;
    zx_rights_t rights;
    status = ChannelDispatcher::Create(&kernel_channel, &user_channel, &rights);
    if (status != ZX_OK)
        return status;

    status = exceptionate->SetChannel(ktl::move(kernel_channel), thread_rights, process_rights);
    if (status != ZX_OK)
        return status;

    // Strip unwanted rights from the user endpoint, exception channels are
    // read-only from userspace.
    //
    // We don't need to remove the task channel if this fails. Exception
    // channels are built to handle the userspace peer closing so it will just
    // follow that path if we fail to copy the userspace endpoint out.
    return out->make(ktl::move(user_channel),
                     rights & (ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_READ));
}

// zx_status_t zx_exception_get_thread
zx_status_t sys_exception_get_thread(zx_handle_t handle, user_out_handle* thread) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ExceptionDispatcher> exception;
    zx_status_t status = up->GetDispatcher(handle, &exception);
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
    zx_status_t status = up->GetDispatcher(handle, &exception);
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
