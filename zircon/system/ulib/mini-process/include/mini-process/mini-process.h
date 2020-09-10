// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zircon/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// mini-process available commands. Use mini_process_cmd() to send them.
//
// The process echoes a canned message.
// The return value upon success is ZX_OK.
#define MINIP_CMD_ECHO_MSG (1 << 0)
// The process creates an event and sends it back on |handle|.
// The return value upon success is ZX_OK.
#define MINIP_CMD_CREATE_EVENT (1 << 1)
// The process creates a profile sends it back on |handle|.
// Because mini-process does not have a handle to the root job, this will always fail.
#define MINIP_CMD_CREATE_PROFILE (1 << 2)
// The process creates a channel and sends one end back on |handle|.
// The return value upon success is ZX_OK.
#define MINIP_CMD_CREATE_CHANNEL (1 << 3)
// The following two commands cause the process to call a syscall with an
// invalid handle value.  The return value is the result of that syscall.
#define MINIP_CMD_USE_BAD_HANDLE_CLOSED (1 << 4)
#define MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED (1 << 5)
// The process will execute __builtin_trap() which causes a fatal exception.
// The return value upon success is ZX_ERR_PEER_CLOSED.
#define MINIP_CMD_BUILTIN_TRAP (1 << 6)
// The process just calls zx_process_exit() immediately without replying.
// The return value upon success is ZX_ERR_PEER_CLOSED.
#define MINIP_CMD_EXIT_NORMAL (1 << 7)
// The process calls zx_object_info( ZX_INFO_HANDLE_VALID) on a closed handle.
#define MINIP_CMD_VALIDATE_CLOSED_HANDLE (1 << 8)
// The process creates a pager vmo and sends it back on |handle|.
#define MINIP_CMD_CREATE_PAGER_VMO (1 << 9)
// The process attempts to create a contiguous vmo and send it back on |handle|.
// This will always fail because we don't supply a bti handle.
#define MINIP_CMD_CREATE_VMO_CONTIGUOUS (1 << 10)
// The process attempts to create a physical vmo and send it back on |handle|.
// This will always fail because we don't supply a mmio resource.
#define MINIP_CMD_CREATE_VMO_PHYSICAL (1 << 11)
// The process writes a single byte 0 to |transferred_handle| with |zx_channel_write|.
// The return value upon success is ZX_OK.
#define MINIP_CMD_CHANNEL_WRITE (1 << 12)
// The process requests a backtrace dump.
// The return value upon successful thread resume is ZX_OK.
#define MINIP_CMD_BACKTRACE_REQUEST (1 << 13)
// The process requests a attempts to use the null handle with replace_as_executable.
// This forwards the result of that operation
#define MINIP_CMD_ATTEMPT_AMBIENT_EXECUTABLE (1 << 14)
// This checks the word the thread register points to against expected value.
#define MINIP_CMD_CHECK_THREAD_POINTER (1 << 15)
// The process will perform an async_wait on the |transferred_handle| and then
// port cancel on it in an infinite loop.
#define MINIP_CMD_WAIT_ASYNC_CANCEL (1 << 16)
#define MINIP_THREAD_POINTER_CHECK_VALUE (0xdeadbeeffeedfaceUL)

// Create and run a minimal process with one thread that blocks forever.
// Does not require a host binary.
zx_status_t start_mini_process(zx_handle_t job, zx_handle_t transferred_handle,
                               zx_handle_t* process, zx_handle_t* thread);

// Like start_mini_process() but requires caller to create the process,
// thread and object to transfer.  Pass NULL in |cntrl_channel| to create
// a minimal process that has no VDSO and loops forever. If |cntrl_channel|
// is valid then upon successful return it contains the handle to a channel
// that the new process is listening to for commands via mini_process_cmd().
// If |wait_for_ack| is false, mini_process_wait_for_ack() must be called
// before mini_process_cmd(); otherwise this blocks until the process has
// started up and read from the control channel.
zx_status_t start_mini_process_etc(zx_handle_t process, zx_handle_t thread, zx_handle_t vmar,
                                   zx_handle_t transferred_handle, bool wait_for_ack,
                                   zx_handle_t* cntrl_channel);

// Loads the vDSO into a process.  |base| and |entry| can be NULL.  This is not
// thread-safe.  It steals the startup handle, so it's not compatible with also
// using launchpad (which also needs to steal the startup handle).
zx_status_t mini_process_load_vdso(zx_handle_t process, zx_handle_t vmar, uintptr_t* base,
                                   uintptr_t* entry);

// Set up a stack VMO mapped into a process.  If |with_code| is true, this
// will include the mini-process code stub.  Otherwise, the stack will not
// be executable.
zx_status_t mini_process_load_stack(zx_handle_t vmar, bool with_code, uintptr_t* stack_base,
                                    uintptr_t* sp);

// Starts a no-VDSO infinite-loop thread.
zx_status_t start_mini_process_thread(zx_handle_t thread, zx_handle_t vmar);

// Consume the reply from a successful start_mini_process_etc() call with
// |wait_for_ack| false.
zx_status_t mini_process_wait_for_ack(zx_handle_t cntrl_channel);

// Execute in the mini process any set of the MINIP_CMD_ commands above.
// The |cntrl_channel| should be the same as the one returned by
// start_mini_process_etc().  The |handle| is an in/out parameter
// dependent on the command.
zx_status_t mini_process_cmd(zx_handle_t cntrl_channel, uint32_t what, zx_handle_t* handle);

// The following pair of functions is equivalent to mini_process_cmd(), but
// they allow sending the request and receiving the reply to be done
// separately.  This allows handling the case where the mini process gets
// suspended as a result of executing the command.
zx_status_t mini_process_cmd_send(zx_handle_t cntrl_channel, uint32_t what);
zx_status_t mini_process_cmd_read_reply(zx_handle_t cntrl_channel, zx_handle_t* handle);

__END_CDECLS
