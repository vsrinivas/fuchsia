// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <lib/zx/exception.h>

struct thread_data_t {
    zx_koid_t tid;
    zx_handle_t handle;
};

// Result of |attach_inferior()|.
struct inferior_data_t {
    // Koid of the inferior process.
    zx_koid_t pid;
    // Borrowed handle of the inferior process.
    zx_handle_t inferior;
    // Borrowed handle of the port listening for signals.
    zx_handle_t port;
    // Owned handle of the exception channel.
    zx_handle_t exception_channel;
    // #entries in |threads|.
    size_t max_num_threads;
    // The array is unsorted, and there can be holes (tid,handle = invalid).
    thread_data_t* threads;
};

typedef bool(wait_inferior_exception_handler_t)(inferior_data_t* data,
                                                const zx_port_packet_t* packet,
                                                void* handler_arg);

void dump_gregs(zx_handle_t thread_handle, const zx_thread_state_general_regs_t* regs);

void dump_inferior_regs(zx_handle_t thread);

void read_inferior_gregs(zx_handle_t thread, zx_thread_state_general_regs_t* regs);

void write_inferior_gregs(zx_handle_t thread, const zx_thread_state_general_regs_t* regs);

size_t read_inferior_memory(zx_handle_t proc, uintptr_t vaddr, void* buf, size_t len);

size_t write_inferior_memory(zx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len);

zx_status_t create_inferior(const char* name, int argc, const char* const* argv,
                            const char* const* envp, size_t hnds_count, zx_handle_t* handles,
                            uint32_t* ids, launchpad_t** out_launchpad);

bool setup_inferior(const char* name, launchpad_t** out_lp, zx_handle_t* out_inferior,
                    zx_handle_t* out_channel);

// Attaches to |inferior| process.
//
// Creates a debug exception channel on |inferior| and uses wait_async() to
// route the following signals through |port|:
//   * process TERMINATED
//   * exception channel READABLE
//   * child thread TERMINATED, RUNNING, and SUSPENDED
// Packet keys are the corresponding object KOIDs.
//
// Returns a newly allocated inferior_data_t, which must be destroyed by
// calling detach_inferior(). On failure, exits the process.
inferior_data_t* attach_inferior(zx_handle_t inferior, zx_handle_t port, size_t max_threads);

bool expect_debugger_attached_eq(zx_handle_t inferior, bool expected, const char* msg);

// Detaches and deletes |data|.
//
// If |close_exception_channel| is false, the exception channel will remain
// open. In this case the caller must have copied |data->exception_channel|
// before calling this function and manually close it when finished.
void detach_inferior(inferior_data_t* data, bool close_exception_channel);

// Closes |data|'s exception channel.
void unbind_inferior(inferior_data_t* data);

bool start_inferior(launchpad_t* lp);

bool shutdown_inferior(zx_handle_t channel, zx_handle_t inferior);

bool read_packet(zx_handle_t port, zx_port_packet_t* packet);

bool wait_thread_suspended(zx_handle_t proc, zx_handle_t thread, zx_handle_t port);

bool handle_thread_exiting(zx_handle_t inferior, const zx_exception_info_t* info,
                           zx::exception exception);

thrd_t start_wait_inf_thread(inferior_data_t* inferior_data,
                             wait_inferior_exception_handler_t* handler,
                             void* handler_arg);

bool join_wait_inf_thread(thrd_t wait_inf_thread);
