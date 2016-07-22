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

#include <magenta/magenta.h>
#include <magenta/msg_pipe_dispatcher.h>
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
// to set the system exception handler?). When a suitable object is created
// this routine can be deleted in favor of set_exception_handler.

mx_status_t sys_set_system_exception_handler(mx_handle_t pipe_handle, mx_exception_behaviour_t behaviour) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> pipe_dispatcher;
    mx_rights_t pipe_rights;
    if (!up->GetDispatcher(pipe_handle, &pipe_dispatcher, &pipe_rights))
        return ERR_INVALID_ARGS;
    // TODO(dje): verify is a pipe of appropriate kind
    // TODO(dje): rights checking of pipe?

    if (behaviour > MX_EXCEPTION_MAX_BEHAVIOUR)
        return ERR_INVALID_ARGS;

    SetSystemExceptionHandler(utils::move(pipe_dispatcher), behaviour);

    return NO_ERROR;
}

mx_status_t sys_set_exception_handler(mx_handle_t object_handle,
                                      mx_handle_t pipe_handle, mx_exception_behaviour_t behaviour) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    utils::RefPtr<Dispatcher> pipe_dispatcher;
    mx_rights_t pipe_rights;
    if (!up->GetDispatcher(pipe_handle, &pipe_dispatcher, &pipe_rights))
        return ERR_INVALID_ARGS;
    // TODO(dje): verify is a pipe of appropriate kind
    // TODO(dje): rights checking of pipe?

    if (behaviour > MX_EXCEPTION_MAX_BEHAVIOUR)
        return ERR_INVALID_ARGS;

    // TODO(dje): Temp hack, 0 == "this process"
    if (object_handle == 0) {
        return up->SetExceptionHandler(utils::move(pipe_dispatcher), behaviour);
    } else {
        // TODO(dje): Rights checking ok?
        utils::RefPtr<Dispatcher> dispatcher;
        mx_rights_t rights;
        if (!up->GetDispatcher(object_handle, &dispatcher, &rights))
            return ERR_INVALID_ARGS;
        // TODO(dje): What's the right right here? [READ is a temp hack]
        if (!magenta_rights_check(rights, MX_RIGHT_READ))
            return ERR_ACCESS_DENIED;

        auto process = dispatcher->get_process_dispatcher();
        if (process)
            return process->SetExceptionHandler(utils::move(pipe_dispatcher), behaviour);

        auto thread = dispatcher->get_thread_dispatcher();
        if (thread)
            return thread->SetExceptionHandler(utils::move(pipe_dispatcher), behaviour);

        return ERR_BAD_HANDLE;
    }
}

mx_status_t sys_mark_exception_handled(mx_handle_t thread_handle, mx_exception_status_t excp_status) {
    LTRACE_ENTRY;

    auto up = ProcessDispatcher::GetCurrent();

    if (excp_status != MX_EXCEPTION_STATUS_NOT_HANDLED &&
        excp_status != MX_EXCEPTION_STATUS_RESUME)
        return ERR_INVALID_ARGS;

    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    if (!up->GetDispatcher(thread_handle, &dispatcher, &rights))
        return ERR_INVALID_ARGS;

    auto thread = dispatcher->get_thread_dispatcher();
    if (!thread)
        return ERR_BAD_HANDLE;

    // TODO(dje): What's the right right here? [READ is a temp hack]
    if (!magenta_rights_check(rights, MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;

    return thread->MarkExceptionHandled(excp_status);
}
