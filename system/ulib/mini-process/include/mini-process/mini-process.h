// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// mini-process available commands. Use mini_process_cmd() to send them.
//
// The process echoes a canned message.
// The return value upon success is NO_ERROR.
#define MINIP_CMD_ECHO_MSG         1
// The process creates an event and sends it back on |handle|.
// The return value upon success is NO_ERROR.
#define MINIP_CMD_CREATE_EVENT     2
// The process creates a channel and sends one end back on |handle|.
// The return value upon success is NO_ERROR.
#define MINIP_CMD_CREATE_CHANNEL   4
// The process will execute __builtin_trap() which causes a fatal exception.
// The return value upon success is ERR_PEER_CLOSED.
#define MINIP_CMD_BUILTIN_TRAP     8
// The process just calls mx_process_exit() immediately without replying.
// The return value upon success is ERR_PEER_CLOSED.
#define MINIP_CMD_EXIT_NORMAL      16

// Create and run a minimal process with one thread that blocks forever.
// Does not require a host binary.
mx_status_t start_mini_process(mx_handle_t job, mx_handle_t transfered_handle,
                               mx_handle_t* process, mx_handle_t* thread);

// Like start_mini_process() but requires caller to create the process,
// thread and object to transfer.  Pass NULL in |cntrl_channel| to create
// a minimal process that has no VDSO and loops forever. If |cntrl_channel|
// is valid then upon successful return it contains the handle to a channel
// that the new process is listening to for commands via mini_process_cmd().
mx_status_t start_mini_process_etc(mx_handle_t process, mx_handle_t thread,
                                   mx_handle_t vmar,
                                   mx_handle_t transfered_handle,
                                   mx_handle_t* cntrl_channel);

// Execute in the mini process any set of the MINIP_CMD_ commands above.
// The |cntrl_channel| should be the same as the one returned by
// start_mini_process_etc().  The |handle| is an in/out parameter
// dependent on the command.
mx_status_t mini_process_cmd(mx_handle_t cntrl_channel,
                             uint32_t what, mx_handle_t* handle);

__END_CDECLS
