// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <runtime/mutex.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef void (*zxr_thread_entry_t)(void*);

typedef struct {
    zxr_thread_entry_t entry;
    zx_handle_t handle;
    atomic_int state;
} zxr_thread_t;

#pragma GCC visibility push(hidden)

// TODO(kulakowski) Document the possible zx_status_t values from these.

// Create a thread, filling in the given zxr_thread_t to describe it.
// The return value is that of zx_thread_create.
// On failure, the zxr_thread_t is clobbered and cannot be passed to
// any functions except zxr_thread_create or zxr_thread_adopt.
// If detached is true, then it's as if zxr_thread_detach were called
// immediately after this returns (but it's more efficient, and can
// never fail with ZX_ERR_BAD_STATE).
zx_status_t zxr_thread_create(zx_handle_t proc_self, const char* name,
                              bool detached, zxr_thread_t* thread);

// Fill in the given zxr_thread_t to describe a thread given its handle.
// This takes ownership of the given thread handle.
zx_status_t zxr_thread_adopt(zx_handle_t handle, zxr_thread_t* thread);

// Start the thread with the given stack, entrypoint, and
// argument. stack_addr is taken to be the low address of the stack
// mapping, and should be page aligned. The size of the stack should
// be a multiple of PAGE_SIZE. When started, the thread will call
// entry(arg).
zx_status_t zxr_thread_start(zxr_thread_t* thread, uintptr_t stack_addr, size_t stack_size, zxr_thread_entry_t entry, void* arg);

// Once started, threads can be either joined or detached. It is undefined
// behavior to join a thread multiple times or to join a detached thread.
// Some of the resources allocated to a thread are not collected until
// it returns and it is either joined or detached.

// If a thread is joined, the caller of zxr_thread_join blocks until
// the other thread is finished running.
zx_status_t zxr_thread_join(zxr_thread_t* thread);

// If a thread is detached, instead of waiting to be joined, it will
// clean up after itself, and the return value of the thread's
// entrypoint is ignored.  This returns ZX_ERR_BAD_STATE if the thread
// had already finished running; it didn't know to clean up after itself
// and it's gone now, so the caller must do any cleanup it would have
// done after zxr_thread_join.  It is undefined behavior to detach
// a thread that has already been joined or to detach an already detached
// thread.
zx_status_t zxr_thread_detach(zxr_thread_t* thread)
    __attribute__((warn_unused_result));

// Indicates whether the thread has been detached.  The result is undefined
// if the thread is exiting or has exited.
bool zxr_thread_detached(zxr_thread_t* thread);

// Exit from the thread.  Equivalent to zxr_thread_exit unless the
// thread has been detached.  If it has been detached, then this does
// zx_vmar_unmap(vmar, addr, len) first, but in a way that permits
// unmapping the caller's own stack.
_Noreturn void zxr_thread_exit_unmap_if_detached(
    zxr_thread_t* thread, zx_handle_t vmar, uintptr_t addr, size_t len);

// Destroy a thread structure that is either created but unstarted or is
// known to belong to a thread that has been zx_task_kill'd and has not been
// joined.  This is only really useful for tests that are intentionally
// bypassing the normal lifecycle of a thread, for handling tests that can't
// detach or join.
// This returns failure if the thread's handle was invalid.
// Regardless, the zxr_thread_t is destroyed.
zx_status_t zxr_thread_destroy(zxr_thread_t* thread);

// Get the zx_handle_t corresponding to the given thread.
// WARNING:
// This is intended for debuggers and so on. Holding this wrong could
// break internal invariants of zxr_thread_t.  It is unsafe to call this
// function from a different thread once this thread is started, if it might
// exit.  The returned handle is not a duplicate, and must be duplicated if the caller
// intends to hold it after zxr_thread_start() is called.
zx_handle_t zxr_thread_get_handle(zxr_thread_t* thread);

#pragma GCC visibility pop

__END_CDECLS
