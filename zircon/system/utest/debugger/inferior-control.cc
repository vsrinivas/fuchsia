// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inferior-control.h"

#include <elf.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "inferior.h"
#include "utils.h"

void dump_gregs(zx_handle_t thread_handle, const zx_thread_state_general_regs_t* regs) {
  unittest_printf("Registers for thread %d\n", thread_handle);

#define DUMP_NAMED_REG(name) \
  unittest_printf("  %8s      %24ld  0x%lx\n", #name, (long)regs->name, (long)regs->name)

#if defined(__x86_64__)

  DUMP_NAMED_REG(rax);
  DUMP_NAMED_REG(rbx);
  DUMP_NAMED_REG(rcx);
  DUMP_NAMED_REG(rdx);
  DUMP_NAMED_REG(rsi);
  DUMP_NAMED_REG(rdi);
  DUMP_NAMED_REG(rbp);
  DUMP_NAMED_REG(rsp);
  DUMP_NAMED_REG(r8);
  DUMP_NAMED_REG(r9);
  DUMP_NAMED_REG(r10);
  DUMP_NAMED_REG(r11);
  DUMP_NAMED_REG(r12);
  DUMP_NAMED_REG(r13);
  DUMP_NAMED_REG(r14);
  DUMP_NAMED_REG(r15);
  DUMP_NAMED_REG(rip);
  DUMP_NAMED_REG(rflags);

#elif defined(__aarch64__)

  for (int i = 0; i < 30; i++) {
    unittest_printf("  r[%2d]     %24ld  0x%lx\n", i, (long)regs->r[i], (long)regs->r[i]);
  }
  DUMP_NAMED_REG(lr);
  DUMP_NAMED_REG(sp);
  DUMP_NAMED_REG(pc);
  DUMP_NAMED_REG(cpsr);

#endif

#undef DUMP_NAMED_REG
}

void dump_inferior_regs(zx_handle_t thread) {
  zx_thread_state_general_regs_t regs;
  read_inferior_gregs(thread, &regs);
  dump_gregs(thread, &regs);
}

// N.B. It is assumed |buf_size| is large enough.

void read_inferior_gregs(zx_handle_t thread, zx_thread_state_general_regs_t* in) {
  zx_status_t status = zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, in,
                                            sizeof(zx_thread_state_general_regs_t));
  // It's easier to just terminate if this fails.
  if (status != ZX_OK)
    tu_fatal("read_inferior_gregs: zx_thread_read_state", status);
}

void write_inferior_gregs(zx_handle_t thread, const zx_thread_state_general_regs_t* out) {
  zx_status_t status = zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, out,
                                             sizeof(zx_thread_state_general_regs_t));
  // It's easier to just terminate if this fails.
  if (status != ZX_OK)
    tu_fatal("write_inferior_gregs: zx_thread_write_state", status);
}

size_t read_inferior_memory(zx_handle_t proc, uintptr_t vaddr, void* buf, size_t len) {
  zx_status_t status = zx_process_read_memory(proc, vaddr, buf, len, &len);
  if (status < 0)
    tu_fatal("read_inferior_memory", status);
  return len;
}

size_t write_inferior_memory(zx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len) {
  zx_status_t status = zx_process_write_memory(proc, vaddr, buf, len, &len);
  if (status < 0)
    tu_fatal("write_inferior_memory", status);
  return len;
}

bool setup_inferior(const char* name, springboard_t** out_sb, zx_handle_t* out_inferior,
                    zx_handle_t* out_channel) {
  BEGIN_HELPER;

  zx_handle_t channel1, channel2;
  ASSERT_EQ(zx_channel_create(0, &channel1, &channel2), ZX_OK);

  const char verbosity_string[] = {'v', '=', static_cast<char>(utest_verbosity_level + '0'), '\0'};
  const char* test_child_path = g_program_path;
  const char* const argv[] = {test_child_path, name, verbosity_string};
  zx_handle_t handles[1] = {channel2};
  uint32_t handle_ids[1] = {PA_USER0};

  unittest_printf("Creating process \"%s\"\n", name);
  springboard_t* sb = tu_launch_init(zx_job_default(), name, std::size(argv), argv, 0, NULL,
                                     std::size(handles), handles, handle_ids);

  // Note: |inferior| is a borrowed handle here.
  zx_handle_t inferior = springboard_get_process_handle(sb);
  ASSERT_NE(inferior, ZX_HANDLE_INVALID, "can't get process handle");

  zx_info_handle_basic_t process_info;
  zx_status_t status = zx_object_get_info(inferior, ZX_INFO_HANDLE_BASIC, &process_info,
                                          sizeof(process_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  unittest_printf("Inferior pid = %llu\n", (long long)process_info.koid);

  *out_sb = sb;
  *out_inferior = inferior;
  *out_channel = channel1;

  END_HELPER;
}

// While this should perhaps take a springboard_t* argument instead of the
// inferior's handle, we later want to test attaching to an already running
// inferior.
// |max_threads| is the maximum number of threads the process is expected
// to have in its lifetime. A real debugger would be more flexible of course.

inferior_data_t* attach_inferior(zx_handle_t inferior, zx_handle_t port, size_t max_threads) {
  // Fetch all current threads and attach async-waiters to them.
  // N.B. We assume threads aren't being created as we're running.
  // This is just a testcase so we can assume that. A real debugger
  // would not have this assumption.
  size_t buffer_size = max_threads * sizeof(zx_koid_t);
  zx_koid_t* thread_koids = reinterpret_cast<zx_koid_t*>(malloc(buffer_size));
  size_t num_threads;
  zx_status_t status = zx_object_get_info(inferior, ZX_INFO_PROCESS_THREADS, thread_koids,
                                          buffer_size, &num_threads, nullptr);
  if (status != ZX_OK)
    tu_fatal(__func__, status);
  // For now require |max_threads| to be big enough.
  if (num_threads > max_threads)
    tu_fatal(__func__, ZX_ERR_BUFFER_TOO_SMALL);

  tu_object_wait_async(inferior, port, ZX_PROCESS_TERMINATED);

  inferior_data_t* data = reinterpret_cast<inferior_data_t*>(malloc(sizeof(*data)));
  data->threads = reinterpret_cast<thread_data_t*>(calloc(max_threads, sizeof(data->threads[0])));
  data->inferior = inferior;
  data->port = port;
  status = zx_task_create_exception_channel(inferior, ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                            &data->exception_channel);
  ZX_ASSERT(status == ZX_OK);
  data->max_num_threads = max_threads;

  // We don't need to listen for ZX_CHANNEL_PEER_CLOSED here because
  // ZX_PROCESS_TERMINATED already tells us when the process terminates.
  tu_object_wait_async(data->exception_channel, port, ZX_CHANNEL_READABLE);

  // Notification of thread termination and suspension is delivered by
  // signals. So that we can continue to only have to wait on |port|
  // for inferior status change notification, install async-waiters
  // for each thread.
  size_t j = 0;
  zx_signals_t thread_signals = ZX_THREAD_TERMINATED | ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;
  for (size_t i = 0; i < num_threads; ++i) {
    zx_handle_t thread;
    zx_status_t status =
        zx_object_get_child(inferior, thread_koids[i], ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status == ZX_ERR_NOT_FOUND) {
      thread = ZX_HANDLE_INVALID;
    } else {
      ZX_ASSERT(status == ZX_OK);
    }
    if (thread != ZX_HANDLE_INVALID) {
      data->threads[j].tid = thread_koids[i];
      data->threads[j].handle = thread;
      tu_object_wait_async(thread, port, thread_signals);
      ++j;
    }
  }
  free(thread_koids);

  unittest_printf("Attached to inferior\n");
  return data;
}

bool expect_debugger_attached_eq(zx_handle_t inferior, bool expected, const char* msg) {
  BEGIN_HELPER;

  zx_info_process_t info;
  // ZX_ASSERT returns false if the check fails.
  ASSERT_EQ(zx_object_get_info(inferior, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK);
  ASSERT_EQ(info.debugger_attached, expected, msg);

  END_HELPER;
}

void detach_inferior(inferior_data_t* data, bool close_exception_channel) {
  if (close_exception_channel) {
    unbind_inferior(data);
  }
  for (size_t i = 0; i < data->max_num_threads; ++i) {
    if (data->threads[i].handle != ZX_HANDLE_INVALID)
      zx_handle_close(data->threads[i].handle);
  }
  free(data->threads);
  free(data);
}

void unbind_inferior(inferior_data_t* data) {
  zx_handle_close(data->exception_channel);
  data->exception_channel = ZX_HANDLE_INVALID;
}

bool start_inferior(springboard_t* sb) {
  tu_launch_fini(sb);
  unittest_printf("Inferior started\n");
  return true;
}

bool shutdown_inferior(zx_handle_t channel, zx_handle_t inferior) {
  BEGIN_HELPER;

  unittest_printf("Shutting down inferior\n");

  send_simple_request(channel, RQST_DONE);

  tu_process_wait_signaled(inferior);
  EXPECT_EQ(tu_process_get_return_code(inferior), kInferiorReturnCode, "");

  END_HELPER;
}

// Wait for and read a packet on |port|.

bool read_packet(zx_handle_t port, zx_port_packet_t* packet) {
  BEGIN_HELPER;

  unittest_printf("read_packet: waiting for signal on port %d\n", port);
  ASSERT_EQ(zx_port_wait(port, ZX_TIME_INFINITE, packet), ZX_OK, "zx_port_wait failed");

  if (ZX_PKT_IS_SIGNAL_ONE(packet->type)) {
    unittest_printf("read_packet: got signal, observed 0x%x\n", packet->signal.observed);
  } else {
    // Leave it to the caller to digest these.
    unittest_printf("read_packet: got other packet %d\n", packet->type);
  }

  END_HELPER;
}

bool wait_thread_state(zx_handle_t proc, zx_handle_t thread, zx_handle_t port,
                       zx_signals_t wait_until) {
  BEGIN_HELPER;

  zx_info_handle_basic_t basic_info;
  zx_status_t status = zx_object_get_info(thread, ZX_INFO_HANDLE_BASIC, &basic_info,
                                          sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t tid = basic_info.koid;

  // The input state we're looking for must be one of the signals we're waiting for. More signals
  // can be added later if needed.
  zx_signals_t signals = ZX_THREAD_TERMINATED | ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;
  ASSERT_TRUE(signals & wait_until);

  tu_object_wait_async(thread, port, signals);
  while (true) {
    zx_port_packet_t packet;
    status = zx_port_wait(port, zx_deadline_after(ZX_SEC(1)), &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      // This shouldn't really happen unless the system is really loaded. Just flag it and try
      // again. The watchdog will catch failures.
      unittest_printf("%s timed out waiting for thread state.\n", __func__);
      continue;
    }
    ASSERT_EQ(status, ZX_OK);
    if (packet.key == tid) {
      if (packet.signal.observed & wait_until)
        break;
      tu_object_wait_async(thread, port, signals);
    }

    // No action necessary if the packet was an exit exception from a previous test, the channel has
    // already been closed so we just needed to pop the packet out of the port.
  }

  zx_info_thread_t info;
  status = zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(info.wait_exception_channel_type, ZX_EXCEPTION_CHANNEL_TYPE_NONE);

  END_HELPER;
}

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

bool handle_thread_exiting(zx_handle_t inferior, const zx_exception_info_t* info,
                           zx::exception exception) {
  BEGIN_HELPER;

  zx::thread thread;
  ASSERT_EQ(exception.get_thread(&thread), ZX_OK);
  zx_info_thread_t thread_info;
  zx_status_t status =
      thread.get_info(ZX_INFO_THREAD, &thread_info, sizeof(thread_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  // The thread could still transition to DEAD here (if the
  // process exits), so check for either DYING or DEAD.
  EXPECT_TRUE(thread_info.state == ZX_THREAD_STATE_DYING ||
              thread_info.state == ZX_THREAD_STATE_DEAD);
  // If the state is DYING it would be nice to check that the
  // value of |info.wait_exception_channel_type| is DEBUGGER. Alas
  // if the process has exited then the thread will get
  // THREAD_SIGNAL_KILL which will cause exception handling to exit
  // before we've told the thread to "resume" from ZX_EXCP_THREAD_EXITING.
  // The thread is still in the DYING state but it is no longer
  // in an exception. Thus |info.wait_exception_channel_type| can
  // either be DEBUGGER or NONE.
  EXPECT_TRUE(thread_info.wait_exception_channel_type == ZX_EXCEPTION_CHANNEL_TYPE_NONE ||
              thread_info.wait_exception_channel_type == ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER);

  // A thread is gone, but we only care about the process.
  unittest_printf("wait-inf: thread %" PRIu64 " exited\n", info->tid);

  END_HELPER;
}

// A simpler exception handler.
// All exceptions are passed on to |handler|.
// Returns false if a test fails.
// Otherwise waits for the inferior to exit and returns true.

static bool wait_inferior_thread_worker(inferior_data_t* inferior_data,
                                        wait_inferior_exception_handler_t* handler,
                                        void* handler_arg) {
  BEGIN_HELPER;

  zx_handle_t inferior = inferior_data->inferior;
  zx_info_handle_basic_t basic_info;
  zx_status_t status = zx_object_get_info(inferior, ZX_INFO_HANDLE_BASIC, &basic_info,
                                          sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t pid = basic_info.koid;
  zx_handle_t port = inferior_data->port;

  while (true) {
    zx_port_packet_t packet;
    if (!read_packet(port, &packet))
      return false;

    // Is the inferior gone?
    if (packet.key == pid) {
      if (packet.signal.observed & ZX_PROCESS_TERMINATED) {
        return true;
      }
      tu_object_wait_async(inferior, port, ZX_PROCESS_TERMINATED);
    } else {
      status = zx_object_get_info(inferior_data->exception_channel, ZX_INFO_HANDLE_BASIC,
                                  &basic_info, sizeof(basic_info), nullptr, nullptr);
      ASSERT_EQ(status, ZX_OK);
      if (packet.key != basic_info.koid) {
        zx_signals_t thread_signals = ZX_THREAD_TERMINATED;
        if (packet.signal.observed & ZX_THREAD_RUNNING)
          thread_signals |= ZX_THREAD_SUSPENDED;
        if (packet.signal.observed & ZX_THREAD_SUSPENDED)
          thread_signals |= ZX_THREAD_RUNNING;
        zx_handle_t thread;
        zx_status_t status =
            zx_object_get_child(inferior, packet.key, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status == ZX_ERR_NOT_FOUND) {
          thread = ZX_HANDLE_INVALID;
        } else {
          ZX_ASSERT(status == ZX_OK);
        }
        if (thread == ZX_HANDLE_INVALID) {
          continue;
        }
        tu_object_wait_async(thread, port, thread_signals);
      }
    }

    bool handler_success = handler(inferior_data, &packet, handler_arg);

    zx_info_handle_basic_t basic_info;
    zx_status_t status = zx_object_get_info(inferior_data->exception_channel, ZX_INFO_HANDLE_BASIC,
                                            &basic_info, sizeof(basic_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    if (packet.key == basic_info.koid) {
      // Don't re-wait on READABLE until after handler() has read the
      // exception out of the channel or it will trigger again
      // immediately.
      //
      // We don't care about PEER_CLOSED here because we're already
      // listening for PROCESS_TERMINATED which gives the same info.
      tu_object_wait_async(inferior_data->exception_channel, port, ZX_CHANNEL_READABLE);
    }

    if (!handler_success) {
      return false;
    }
  }

  END_HELPER;
}

struct wait_inferior_args_t {
  inferior_data_t* inferior_data;
  wait_inferior_exception_handler_t* handler;
  void* handler_arg;
};

static int wait_inferior_thread_func(void* arg) {
  wait_inferior_args_t* args = reinterpret_cast<wait_inferior_args_t*>(arg);
  inferior_data_t* inferior_data = args->inferior_data;
  wait_inferior_exception_handler_t* handler = args->handler;
  void* handler_arg = args->handler_arg;
  free(args);

  bool pass = wait_inferior_thread_worker(inferior_data, handler, handler_arg);

  return pass ? 0 : -1;
}

thrd_t start_wait_inf_thread(inferior_data_t* inferior_data,
                             wait_inferior_exception_handler_t* handler, void* handler_arg) {
  wait_inferior_args_t* args = reinterpret_cast<wait_inferior_args_t*>(calloc(1, sizeof(*args)));

  // The proc handle is loaned to the thread.
  // The caller of this function owns and must close it.
  args->inferior_data = inferior_data;
  args->handler = handler;
  args->handler_arg = handler_arg;

  thrd_t wait_inferior_thread;
  int ret = thrd_create_with_name(&wait_inferior_thread, wait_inferior_thread_func, args,
                                  "wait-inf thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);
  return wait_inferior_thread;
}

bool join_wait_inf_thread(thrd_t wait_inf_thread) {
  BEGIN_HELPER;

  unittest_printf("Waiting for wait-inf thread\n");
  int thread_rc;
  int ret = thrd_join(wait_inf_thread, &thread_rc);
  EXPECT_EQ(ret, thrd_success, "thrd_join failed");
  EXPECT_EQ(thread_rc, 0, "unexpected wait-inf return");
  unittest_printf("wait-inf thread done\n");

  END_HELPER;
}
