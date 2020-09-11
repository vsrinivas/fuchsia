// Copyright 2017 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ** WARNING ** WARNING ** WARNING ** WARNING ** WARNING **
//
// The following functions are called with only a basic environment set up.
// In particular, most of Fuchsia (such as the C libraries) are compiled
// using split stacks, but these threads run in a context without support for this.
// Calling a function that uses the split stack will result in a crash (possibly
// only on one architecture, on one compiler, at one optimization level).
// As a result, we can only use very basic functions here, and can't call into
// the C library.
//
// Avoid adding any #include's here, especially those from the C or C++ standard
// libraries.
#include "thread-functions.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
// ** WARNING ** WARNING ** WARNING ** WARNING ** WARNING **

namespace {

// Determine if the two given buffers are equal.
//
// This is equivalent to "memcmp(a, b, size) == 0", but we can't call the standard library from
// these functions.
bool buffers_equal(const uint8_t* a, const uint8_t* b, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

void threads_test_sleep_fn(void* arg) {
  // Note: You shouldn't use C standard library functions from this thread.
  zx_time_t time = (zx_time_t)arg;
  zx_nanosleep(time);
}

void threads_test_wait_fn(void* arg) {
  zx_handle_t event = *(zx_handle_t*)arg;
  zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);
  zx_object_signal(event, 0u, ZX_USER_SIGNAL_1);
}

void threads_test_wait_detach_fn(void* arg) {
  threads_test_wait_fn(arg);
  // Since we're detached, we are not allowed to return into the default zxr_thread
  // exit path.
  zx_thread_exit();
}

void threads_test_wait_break_fn(void* arg) {
  zx_handle_t event = *(zx_handle_t*)arg;
  zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);

  // Don't use builtin_trap since the compiler might assume everything after that call can't
  // execute and might remove the function epilog. The test harness will catch the exception
  // and step over it.
#if defined(__aarch64__)
  __asm__ volatile("brk 0");
#elif defined(__x86_64__)
  __asm__ volatile("int3");
#else
#error Not supported on this platform.
#endif
  zx_thread_exit();
}

void threads_test_infinite_wait_fn(void* arg) {
  zx_handle_t event = *(zx_handle_t*)arg;
  zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);
  __builtin_trap();
}

void threads_test_port_fn(void* arg) {
  zx_handle_t* port = (zx_handle_t*)arg;
  zx_port_packet_t packet = {};
  zx_port_wait(port[0], ZX_TIME_INFINITE, &packet);
  packet.key += 5u;
  zx_port_queue(port[1], &packet);
}

void threads_test_channel_call_fn(void* arg_) {
  channel_call_suspend_test_arg* arg = static_cast<channel_call_suspend_test_arg*>(arg_);

  uint8_t send_buf[9] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
  uint8_t recv_buf[9];
  uint32_t actual_bytes, actual_handles;

  zx_channel_call_args_t call_args = {
      .wr_bytes = send_buf,
      .wr_handles = NULL,
      .rd_bytes = recv_buf,
      .rd_handles = NULL,
      .wr_num_bytes = sizeof(send_buf),
      .wr_num_handles = 0,
      .rd_num_bytes = sizeof(recv_buf),
      .rd_num_handles = 0,
  };

  arg->call_status = zx_channel_call(arg->channel, 0, ZX_TIME_INFINITE, &call_args, &actual_bytes,
                                     &actual_handles);
  if (arg->call_status == ZX_OK) {
    if (actual_bytes != sizeof(recv_buf) ||
        !buffers_equal(recv_buf + sizeof(zx_txid_t),
                       reinterpret_cast<const uint8_t*>(&"abcdefghj"[sizeof(zx_txid_t)]),
                       sizeof(recv_buf) - sizeof(zx_txid_t))) {
      arg->call_status = ZX_ERR_BAD_STATE;
    }
  }

  zx_handle_close(arg->channel);
}

void atomic_store(volatile int* addr, int value) {
  __atomic_store_n(addr, value, __ATOMIC_SEQ_CST);
}

int atomic_load(volatile int* addr) { return __atomic_load_n(addr, __ATOMIC_SEQ_CST); }

int atomic_exchange(volatile int* addr, int value) {
  return __atomic_exchange_n(addr, value, __ATOMIC_SEQ_CST);
}

void threads_test_atomic_store(void* arg) {
  auto* p = static_cast<volatile int*>(arg);
  while (atomic_exchange(p, kTestAtomicSetValue) != kTestAtomicExitValue) {
  }
}

void threads_test_run_fn(void* arg) {
  zx_handle_t event = *(zx_handle_t*)arg;
  zx_object_signal(event, 0u, ZX_USER_SIGNAL_0);
  zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL);
}
