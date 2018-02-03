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

enum message {
    // Force the type to be signed, avoids mismatch clashes in unittest macros.
    MSG_FORCE_SIGNED = -1,
    MSG_DONE,
    MSG_PING,
    MSG_PONG,
    MSG_CRASH_AND_RECOVER_TEST,
    MSG_RECOVERED_FROM_CRASH,
    MSG_START_EXTRA_THREADS,
    MSG_EXTRA_THREADS_STARTED,
    MSG_GET_THREAD_HANDLE,
    MSG_THREAD_HANDLE,
};

typedef struct {
    zx_koid_t tid;
    zx_handle_t handle;
} thread_data_t;

typedef struct {
    // Koid of the inferior process.
    zx_koid_t pid;
    // Borrowed handle of the inferior process.
    zx_handle_t inferior;
    // Borrowed handle of the exception port.
    zx_handle_t eport;
    // #entries in |threads|.
    size_t max_num_threads;
    // The array is unsorted, and there can be holes (tid,handle = invalid).
    thread_data_t* threads;
} inferior_data_t;

extern const char* program_path;

extern uint32_t get_uint32(char* buf);

extern uint64_t get_uint64(char* buf);

extern void set_uint64(char* buf, uint64_t value);

extern uint32_t get_uint32_property(zx_handle_t handle, uint32_t prop);

extern void send_msg(zx_handle_t handle, enum message msg);

extern bool recv_msg(zx_handle_t handle, enum message* msg);

extern void dump_gregs(zx_handle_t thread_handle, const zx_thread_state_general_regs_t* regs);

extern void dump_inferior_regs(zx_handle_t thread);

extern void read_inferior_gregs(zx_handle_t thread, zx_thread_state_general_regs_t* out);

extern void write_inferior_gregs(zx_handle_t thread, const zx_thread_state_general_regs_t* out);

extern size_t read_inferior_memory(zx_handle_t proc, uintptr_t vaddr, void* buf, size_t len);

extern size_t write_inferior_memory(zx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len);

extern zx_status_t create_inferior(const char* name, int argc, const char* const* argv,
                                   const char* const* envp, size_t hnds_count, zx_handle_t* handles,
                                   uint32_t* ids, launchpad_t** out_launchpad);

extern bool setup_inferior(const char* name, launchpad_t** out_lp, zx_handle_t* out_inferior,
                           zx_handle_t* out_channel);

extern inferior_data_t* attach_inferior(zx_handle_t inferior, zx_handle_t eport,
                                        size_t max_threads);

extern void detach_inferior(inferior_data_t* data, bool unbind_eport);

extern bool start_inferior(launchpad_t* lp);

extern bool verify_inferior_running(zx_handle_t channel);

extern bool get_inferior_thread_handle(zx_handle_t channel, zx_handle_t* thread);

extern bool resume_inferior(zx_handle_t inferior, zx_koid_t tid);

extern bool shutdown_inferior(zx_handle_t channel, zx_handle_t inferior);

extern bool read_exception(zx_handle_t eport, zx_port_packet_t* packet);

extern bool wait_thread_suspended(zx_handle_t proc, zx_handle_t thread, zx_handle_t eport);

extern bool get_vdso_exec_range(uintptr_t* start, uintptr_t* end);
