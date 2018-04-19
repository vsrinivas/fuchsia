// Copyright 2017 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <runtime/thread.h>

#include "threads.h"

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

void threads_test_wait_break_infinite_sleep_fn(void* arg) {
    zx_handle_t event = *(zx_handle_t*)arg;
    zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);

    // Don't use builtin_trap since the compiler might assume everything after that call can't
    // execute and will remove the zx_nanosleep below.
#if defined(__aarch64__)
    __asm__ volatile("brk 0");
#elif defined(__x86_64__)
    __asm__ volatile("int3");
#else
#error Not supported on this platform.
#endif

    zx_nanosleep(UINT64_MAX);
}

void threads_test_busy_fn(void* arg) {
    volatile uint64_t i = 0u;
    while (true) {
        ++i;
    }
    __builtin_trap();
}

void threads_test_infinite_sleep_fn(void* arg) {
    zx_nanosleep(UINT64_MAX);
    __builtin_trap();
}

void threads_test_infinite_wait_fn(void* arg) {
    zx_handle_t event = *(zx_handle_t*)arg;
    zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);
    __builtin_trap();
}

void threads_test_port_fn(void* arg) {
    zx_handle_t* port = (zx_handle_t*)arg;
    zx_port_packet_t packet = {};
    zx_port_wait(port[0], ZX_TIME_INFINITE, &packet, 1u);
    packet.key += 5u;
    zx_port_queue(port[1], &packet, 1u);
}

void threads_test_channel_call_fn(void* arg_) {
    struct channel_call_suspend_test_arg* arg = arg_;

    uint8_t send_buf[9] = "abcdefghi";
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

    arg->read_status = ZX_OK;
    arg->call_status = zx_channel_call(arg->channel, 0, ZX_TIME_INFINITE, &call_args,
                                       &actual_bytes, &actual_handles, &arg->read_status);

    if (arg->call_status == ZX_OK) {
        arg->read_status = ZX_OK;
        if (actual_bytes != sizeof(recv_buf) || memcmp(recv_buf, "abcdefghj", sizeof(recv_buf))) {
            arg->call_status = ZX_ERR_BAD_STATE;
        }
    }

    zx_handle_close(arg->channel);
}
