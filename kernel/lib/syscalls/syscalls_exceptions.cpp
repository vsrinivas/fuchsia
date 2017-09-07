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

#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

static mx_status_t object_unbind_exception_port(mx_handle_t obj_handle, bool debugger, bool quietly) {
    // TODO(MG-968): check rights once appropriate right is determined

    if (obj_handle == MX_HANDLE_INVALID) {
        // TODO(MG-987): handle for system exception
        if (debugger || quietly)
            return MX_ERR_INVALID_ARGS;
        return ResetSystemExceptionPort()
                   ? MX_OK
                   : MX_ERR_BAD_STATE;  // No port was bound.
    }

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != MX_OK)
        return status;

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        if (debugger)
            return MX_ERR_INVALID_ARGS;
        return job->ResetExceptionPort(quietly)
                   ? MX_OK
                   : MX_ERR_BAD_STATE;  // No port was bound.
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        return process->ResetExceptionPort(debugger, quietly)
                   ? MX_OK
                   : MX_ERR_BAD_STATE;  // No port was bound.
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return MX_ERR_INVALID_ARGS;
        return thread->ResetExceptionPort(quietly)
                   ? MX_OK
                   : MX_ERR_BAD_STATE;  // No port was bound.
    }

    return MX_ERR_WRONG_TYPE;
}

static mx_status_t task_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle, uint64_t key, bool debugger) {
    // TODO(MG-968): check rights once appropriate right is determined
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcher(eport_handle, &port);
    if (status != MX_OK)
        return status;

    fbl::RefPtr<ExceptionPort> eport;

    if (obj_handle == MX_HANDLE_INVALID) {
        // TODO(MG-987): handle for system exception
        if (debugger)
            return MX_ERR_INVALID_ARGS;
        status = ExceptionPort::Create(ExceptionPort::Type::JOB,
                                       fbl::move(port), key, &eport);
        if (status != MX_OK)
            return status;

        return SetSystemExceptionPort(eport);
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != MX_OK)
        return status;

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        if (debugger)
            return MX_ERR_INVALID_ARGS;
        status = ExceptionPort::Create(ExceptionPort::Type::JOB,
                                       fbl::move(port), key, &eport);
        if (status != MX_OK)
            return status;
        status = job->SetExceptionPort(eport);
        if (status != MX_OK)
            return status;

        eport->SetTarget(job);
        return MX_OK;
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        ExceptionPort::Type type;
        if (debugger)
            type = ExceptionPort::Type::DEBUGGER;
        else
            type = ExceptionPort::Type::PROCESS;
        status = ExceptionPort::Create(type, fbl::move(port), key, &eport);
        if (status != MX_OK)
            return status;
        status = process->SetExceptionPort(eport);
        if (status != MX_OK)
            return status;

        eport->SetTarget(process);
        return MX_OK;
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return MX_ERR_INVALID_ARGS;
        status = ExceptionPort::Create(ExceptionPort::Type::THREAD,
                                       fbl::move(port), key, &eport);
        if (status != MX_OK)
            return status;
        status = thread->SetExceptionPort(eport);
        if (status != MX_OK)
            return status;

        eport->SetTarget(thread);
        return MX_OK;
    }

    return MX_ERR_WRONG_TYPE;
}

mx_status_t sys_task_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle,
                                           uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (eport_handle == MX_HANDLE_INVALID) {
        if (options & ~(MX_EXCEPTION_PORT_DEBUGGER + MX_EXCEPTION_PORT_UNBIND_QUIETLY))
            return MX_ERR_INVALID_ARGS;
    } else {
        if (options & ~MX_EXCEPTION_PORT_DEBUGGER)
            return MX_ERR_INVALID_ARGS;
    }

    bool debugger = (options & MX_EXCEPTION_PORT_DEBUGGER) != 0;

    if (eport_handle == MX_HANDLE_INVALID) {
        bool quietly = (options & MX_EXCEPTION_PORT_UNBIND_QUIETLY) != 0;
        return object_unbind_exception_port(obj_handle, debugger, quietly);
    } else {
        return task_bind_exception_port(obj_handle, eport_handle, key, debugger);
    }
}

mx_status_t sys_task_resume(mx_handle_t handle, uint32_t options) {
    LTRACE_ENTRY;

    if (options & ~(MX_RESUME_EXCEPTION | MX_RESUME_TRY_NEXT))
        return MX_ERR_INVALID_ARGS;
    if (!(options & MX_RESUME_EXCEPTION)) {
        // These options are only valid with MX_RESUME_EXCEPTION.
        if (options & MX_RESUME_TRY_NEXT)
            return MX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(MG-968): Rights checking here
    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != MX_OK)
        return status;

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (!thread)
        return MX_ERR_WRONG_TYPE;

    if (options & MX_RESUME_EXCEPTION) {
        ThreadDispatcher::ExceptionStatus estatus;
        if (options & MX_RESUME_TRY_NEXT) {
            estatus = ThreadDispatcher::ExceptionStatus::TRY_NEXT;
        } else {
            estatus = ThreadDispatcher::ExceptionStatus::RESUME;
        }
        return thread->MarkExceptionHandled(estatus);
    } else {
        if (options != 0) {
            return MX_ERR_INVALID_ARGS;
        }

        return thread->Resume();
    }
}
