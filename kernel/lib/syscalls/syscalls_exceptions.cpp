// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <lib/console.h>
#include <lib/user_copy.h>
#include <list.h>

#include <magenta/io_port_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <utils/string_piece.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

// TODO(dje): This is a temp hack as there is no handle of an object
// provided on which to do rights checking (does the caller have permission
// to set the system exception port?). When a suitable object that represents
// the system in this context is created this routine can be deleted in favor
// of set_exception_port.

mx_status_t sys_set_system_exception_port(mx_handle_t handle, uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (options != 0)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(dje): Rights checking is missing because remember this is just
    // temporary code until there is an object representing the system.

    if (handle == MX_HANDLE_INVALID) {
        ResetSystemExceptionPort();
        return NO_ERROR;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_BAD_HANDLE;

    auto ioport = dispatcher->get_io_port_dispatcher();
    if (!ioport)
        return ERR_WRONG_TYPE;

    mxtl::RefPtr<ExceptionPort> eport;
    mx_status_t status = ExceptionPort::Create(mxtl::RefPtr<IOPortDispatcher>(ioport), key, &eport);
    if (status != NO_ERROR)
        return status;

    return SetSystemExceptionPort(mxtl::move(eport));
}

mx_status_t sys_set_exception_port(mx_handle_t object_handle, mx_handle_t eport_handle,
                                   uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (options != 0)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    // The exception port is removed by passing MX_HANDLE_INVALID for the
    // eport.

    if (eport_handle == MX_HANDLE_INVALID) {
        // TODO(dje): Rights checking is missing because remember this is just
        // temporary code until there is an object representing "self".
        if (object_handle == MX_HANDLE_INVALID) {
            up->ResetExceptionPort();
            return NO_ERROR;
        } else {
            mxtl::RefPtr<Dispatcher> dispatcher;
            mx_rights_t rights;
            if (!up->GetDispatcher(object_handle, &dispatcher, &rights))
                return ERR_BAD_HANDLE;
            // TODO(dje): What's the right right here? [READ is a temp hack]
            if (!magenta_rights_check(rights, MX_RIGHT_READ))
                return ERR_ACCESS_DENIED;

            auto process = dispatcher->get_process_dispatcher();
            if (process) {
                process->ResetExceptionPort();
                return NO_ERROR;
            }

            auto thread = dispatcher->get_thread_dispatcher();
            if (thread) {
                thread->ResetExceptionPort();
                return NO_ERROR;
            }

            return ERR_WRONG_TYPE;
        }
    }

    mxtl::RefPtr<Dispatcher> ioport_dispatcher;
    mx_rights_t ioport_rights;
    if (!up->GetDispatcher(eport_handle, &ioport_dispatcher, &ioport_rights))
        return ERR_BAD_HANDLE;
    auto ioport = ioport_dispatcher->get_io_port_dispatcher();
    if (!ioport)
        return ERR_WRONG_TYPE;
    // TODO(dje): rights checking of ioport

    mxtl::RefPtr<ExceptionPort> eport;
    mx_status_t status = ExceptionPort::Create(mxtl::RefPtr<IOPortDispatcher>(ioport), key, &eport);
    if (status != NO_ERROR)
        return status;

    // TODO(dje): Temp hack, MX_HANDLE_INVALID == "this process"
    if (object_handle == MX_HANDLE_INVALID) {
        return up->SetExceptionPort(mxtl::move(eport));
    } else {
        mxtl::RefPtr<Dispatcher> dispatcher;
        mx_rights_t rights;
        if (!up->GetDispatcher(object_handle, &dispatcher, &rights))
            return ERR_BAD_HANDLE;
        // TODO(dje): What's the right right here? [READ is a temp hack]
        if (!magenta_rights_check(rights, MX_RIGHT_READ))
            return ERR_ACCESS_DENIED;

        auto process = dispatcher->get_process_dispatcher();
        if (process)
            return process->SetExceptionPort(mxtl::move(eport));

        auto thread = dispatcher->get_thread_dispatcher();
        if (thread)
            return thread->SetExceptionPort(mxtl::move(eport));

        return ERR_WRONG_TYPE;
    }
}

mx_status_t sys_mark_exception_handled(mx_handle_t handle, mx_koid_t tid, mx_exception_status_t excp_status) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    switch (excp_status)
    {
    case MX_EXCEPTION_STATUS_NOT_HANDLED:
    case MX_EXCEPTION_STATUS_RESUME:
    case MX_EXCEPTION_STATUS_HANDLER_GONE: // TODO(dje): ???
        break;
    default:
        return ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_BAD_HANDLE;

    auto process = dispatcher->get_process_dispatcher();
    if (!process)
        return ERR_WRONG_TYPE;

    // TODO(dje): What's the right right here? [READ is a temp hack]
    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    // The thread must come from |process|.
    auto thread = process->LookupThreadById(tid);
    if (!thread)
        return ERR_INVALID_ARGS;

    return thread->MarkExceptionHandled(excp_status);
}

// Make a handle of |process| that can be used for debugging it.

static mx_handle_t make_process_handle(mxtl::RefPtr<ProcessDispatcher> process) {
    auto up = ProcessDispatcher::GetCurrent();

    // Remember, even this wip is just for bootstrapping purposes.
    mx_rights_t process_rights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DEBUG; // TODO(dje): wip
    mxtl::RefPtr<Dispatcher> dispatcher(process.get());
    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), process_rights));
    if (!handle)
        return ERR_NO_MEMORY;
    mx_handle_t hv = up->MapHandleToValue(handle.get());
    up->AddHandle(mxtl::move(handle));
    return hv;
}

static mx_handle_t sys_process_lookup_worker(mx_koid_t pid) {
    auto process = ProcessDispatcher::LookupProcessById(pid);
    if (!process)
        return ERR_INVALID_ARGS;
    return make_process_handle(process);
}

mx_handle_t sys_process_debug(mx_handle_t handle, mx_koid_t pid) {
    LTRACE_ENTRY;

    // TODO(dje): rights checking

    // TODO(dje): Quick hack for system exception handlers. Can go away when
    // the system exception handler has its own process handle.
    if (handle == MX_HANDLE_INVALID)
        return sys_process_lookup_worker(pid);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_BAD_HANDLE;

    // A thread may be the exception handler for a process.
    auto thread = dispatcher->get_thread_dispatcher();
    if (thread) {
        mxtl::RefPtr<ProcessDispatcher> process(thread->thread()->process());
        if (process->get_koid() == pid)
            return make_process_handle(mxtl::move(process));
        return ERR_INVALID_ARGS;
    }

    // The only remaining possibility for the type of handle is a process,
    // but it's a TODO to establish whether the |process| has debug rights
    // of |pid|. For now just verify we got a process.
    auto process = dispatcher->get_process_dispatcher();
    if (!process)
        return ERR_WRONG_TYPE;

    return sys_process_lookup_worker(pid);
}
