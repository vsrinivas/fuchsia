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

#include "priv.h"

#define LOCAL_TRACE 0

static zx_status_t object_unbind_exception_port(zx_handle_t obj_handle, bool debugger, bool quietly) {
    // TODO(ZX-968): check rights once appropriate right is determined
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    auto job = DownCastDispatcher<JobDispatcher>(&dispatcher);
    if (job) {
        if (debugger)
            return ZX_ERR_INVALID_ARGS;
        return job->ResetExceptionPort(quietly)
                   ? ZX_OK
                   : ZX_ERR_BAD_STATE;  // No port was bound.
    }

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        return process->ResetExceptionPort(debugger, quietly)
                   ? ZX_OK
                   : ZX_ERR_BAD_STATE;  // No port was bound.
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return ZX_ERR_INVALID_ARGS;
        return thread->ResetExceptionPort(quietly)
                   ? ZX_OK
                   : ZX_ERR_BAD_STATE;  // No port was bound.
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
        if (debugger)
            return ZX_ERR_INVALID_ARGS;
        status = ExceptionPort::Create(ExceptionPort::Type::JOB,
                                       fbl::move(port), key, &eport);
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
        status = ExceptionPort::Create(type, fbl::move(port), key, &eport);
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
                                       fbl::move(port), key, &eport);
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

zx_status_t sys_task_bind_exception_port(zx_handle_t obj_handle, zx_handle_t eport_handle,
                                           uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (eport_handle == ZX_HANDLE_INVALID) {
        if (options & ~(ZX_EXCEPTION_PORT_DEBUGGER + ZX_EXCEPTION_PORT_UNBIND_QUIETLY))
            return ZX_ERR_INVALID_ARGS;
    } else {
        if (options & ~ZX_EXCEPTION_PORT_DEBUGGER)
            return ZX_ERR_INVALID_ARGS;
    }

    bool debugger = (options & ZX_EXCEPTION_PORT_DEBUGGER) != 0;

    if (eport_handle == ZX_HANDLE_INVALID) {
        bool quietly = (options & ZX_EXCEPTION_PORT_UNBIND_QUIETLY) != 0;
        return object_unbind_exception_port(obj_handle, debugger, quietly);
    } else {
        return task_bind_exception_port(obj_handle, eport_handle, key, debugger);
    }
}

zx_status_t sys_task_resume(zx_handle_t handle, uint32_t options) {
    LTRACE_ENTRY;

    if (options & ~(ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT))
        return ZX_ERR_INVALID_ARGS;
    if (!(options & ZX_RESUME_EXCEPTION)) {
        // These options are only valid with ZX_RESUME_EXCEPTION.
        if (options & ZX_RESUME_TRY_NEXT)
            return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(ZX-968): Rights checking here
    fbl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != ZX_OK)
        return status;

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (!thread)
        return ZX_ERR_WRONG_TYPE;

    if (options & ZX_RESUME_EXCEPTION) {
        ThreadDispatcher::ExceptionStatus estatus;
        if (options & ZX_RESUME_TRY_NEXT) {
            estatus = ThreadDispatcher::ExceptionStatus::TRY_NEXT;
        } else {
            estatus = ThreadDispatcher::ExceptionStatus::RESUME;
        }
        return thread->MarkExceptionHandled(estatus);
    } else {
        if (options != 0) {
            return ZX_ERR_INVALID_ARGS;
        }

        return thread->Resume();
    }
}

zx_status_t sys_task_resume_from_exception(zx_handle_t task_handle, zx_handle_t eport_handle,
                                           uint32_t options) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ThreadDispatcher> thread;
    zx_status_t status = up->GetDispatcher(task_handle, &thread);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<PortDispatcher> eport;
    status = up->GetDispatcher(eport_handle, &eport);
    if (status != ZX_OK)
        return status;

    // Currently the only option is the ZX_RESUME_TRY_NEXT bit.
    if (options != 0 && options != ZX_RESUME_TRY_NEXT)
        return ZX_ERR_INVALID_ARGS;

    ThreadDispatcher::ExceptionStatus estatus;
    if (options & ZX_RESUME_TRY_NEXT) {
        estatus = ThreadDispatcher::ExceptionStatus::TRY_NEXT;
    } else {
        estatus = ThreadDispatcher::ExceptionStatus::RESUME;
    }
    return thread->MarkExceptionHandled(eport.get(), estatus);
}
