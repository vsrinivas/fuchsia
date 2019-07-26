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

// Requests are sent from the "debugger" to the inferior.
enum request_t {
  // Force the type to be signed, avoids mismatch clashes in unittest macros.
  RQST_FORCE_SIGNED = -1,
  RQST_DONE,
  RQST_PING,
  RQST_CRASH_AND_RECOVER_TEST,
  RQST_START_LOOPING_THREADS,
  RQST_START_CAPTURE_REGS_THREADS,
  RQST_GET_THREAD_HANDLE,
  RQST_GET_LOAD_ADDRS,
};

// Responses are sent from the inferior back to the debugger.
enum response_t {
  // Force the type to be signed, avoids mismatch clashes in unittest macros.
  RESP_FORCE_SIGNED = -1,
  RESP_PONG,
  RESP_RECOVERED_FROM_CRASH,
  RESP_THREADS_STARTED,
  RESP_THREAD_HANDLE,
  RESP_LOAD_ADDRS,
};

// Union of all possible requests.
struct request_message_t {
  request_t type;
};

// Used to pass the response to a RQST_GET_LOAD_ADDRS request.
struct load_addrs_response_t {
  zx_vaddr_t libc_load_addr;
  zx_vaddr_t exec_load_addr;
};

// Union of all possible responses.
struct response_message_t {
  response_t type;
  union {
    load_addrs_response_t load_addrs;
  } payload;
  zx_handle_t handle;
};

extern const char* g_program_path;

static inline void undefined_insn() {
#if defined(__x86_64__)
  __asm__("ud2");
#elif defined(__aarch64__)
  // An instruction not supported at this privilege level will do.
  // ARM calls these "unallocated instructions".
  __asm__("mrs x0, elr_el1");
#else
#error "unsupported architecture"
#endif
}

uint64_t extract_pc_reg(const zx_thread_state_general_regs_t* regs);

uint64_t extract_sp_reg(const zx_thread_state_general_regs_t* regs);

uint32_t get_uint32(char* buf);

uint64_t get_uint64(char* buf);

void set_uint64(char* buf, uint64_t value);

uint32_t get_uint32_property(zx_handle_t handle, uint32_t prop);

void send_request(zx_handle_t handle, const request_message_t& rqst);

void send_simple_request(zx_handle_t handle, request_t rqst);

void send_response(zx_handle_t handle, const response_message_t& resp);

void send_response_with_handle(zx_handle_t handle, const response_message_t& resp,
                               zx_handle_t resp_handle);

void send_simple_response(zx_handle_t handle, response_t resp);

bool recv_request(zx_handle_t handle, request_message_t* rqst);

bool recv_response(zx_handle_t handle, response_message_t* resp);

bool recv_simple_response(zx_handle_t handle, response_t expected_type);

bool verify_inferior_running(zx_handle_t channel);

bool get_inferior_thread_handle(zx_handle_t channel, zx_handle_t* thread);

bool get_vdso_exec_range(uintptr_t* start, uintptr_t* end);

// Return executable's load address.
zx_vaddr_t get_exec_load_addr();

// Return libc's load address.
zx_vaddr_t get_libc_load_addr();

// Fetch the inferior's libc and exec load addresses.
bool get_inferior_load_addrs(zx_handle_t channel, zx_vaddr_t* libc_load_addr,
                             zx_vaddr_t* exec_load_addr);

// Return libc's entry point as found in its in-process ELF header.
zx_vaddr_t get_libc_entry_point();
