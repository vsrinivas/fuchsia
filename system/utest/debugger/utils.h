// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>

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
};

extern const char* program_path;

extern uint32_t get_uint32(char* buf);

extern uint64_t get_uint64(char* buf);

extern void set_uint64(char* buf, uint64_t value);

extern uint32_t get_uint32_property(mx_handle_t handle, uint32_t prop);

extern void send_msg(mx_handle_t handle, enum message msg);

extern bool recv_msg(mx_handle_t handle, enum message* msg);

extern void dump_gregs(mx_handle_t thread_handle, void* buf);

extern void dump_arch_regs (mx_handle_t thread_handle, int regset, void* buf);

extern bool dump_inferior_regs(mx_handle_t thread);

extern uint32_t get_inferior_greg_buf_size(mx_handle_t thread);

extern void read_inferior_gregs(mx_handle_t thread, void* buf, unsigned buf_size);

extern void write_inferior_gregs(mx_handle_t thread, const void* buf, unsigned buf_size);

extern uint64_t get_uint64_register(mx_handle_t thread, size_t offset);

extern void set_uint64_register(mx_handle_t thread, size_t offset, uint64_t value);

extern size_t read_inferior_memory(mx_handle_t proc, uintptr_t vaddr, void* buf, size_t len);

extern size_t write_inferior_memory(mx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len);

extern mx_status_t create_inferior(const char* name,
                                   int argc, const char* const* argv,
                                   const char* const* envp,
                                   size_t hnds_count, mx_handle_t* handles,
                                   uint32_t* ids, launchpad_t** out_launchpad);

extern bool setup_inferior(const char* name,
                           launchpad_t** out_lp,
                           mx_handle_t* out_inferior,
                           mx_handle_t* out_channel);

extern mx_handle_t attach_inferior(mx_handle_t inferior);

extern bool start_inferior(launchpad_t* lp);

extern bool verify_inferior_running(mx_handle_t channel);

extern bool resume_inferior(mx_handle_t inferior, mx_koid_t tid);

extern bool shutdown_inferior(mx_handle_t channel, mx_handle_t inferior);

extern bool read_exception(mx_handle_t eport, mx_handle_t inferior,
                           mx_port_packet_t* packet);
