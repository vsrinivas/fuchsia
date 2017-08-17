// Copyright 2017 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>

#include <runtime/thread.h>

#include "threads.h"

void threads_test_sleep_fn(void* arg) {
    // Note: You shouldn't use C standard library functions from this thread.
    mx_time_t time = (mx_time_t)arg;
    mx_nanosleep(time);
}

void threads_test_wait_fn(void* arg) {
    mx_handle_t event = *(mx_handle_t*)arg;
    mx_object_wait_one(event, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL);
    mx_object_signal(event, 0u, MX_USER_SIGNAL_1);
}

void threads_test_wait_detach_fn(void* arg) {
    threads_test_wait_fn(arg);
    // Since we're detached, we are not allowed to return into the default mxr_thread
    // exit path.
    mx_thread_exit();
}

void threads_test_busy_fn(void* arg) {
    volatile uint64_t i = 0u;
    while (true) {
        ++i;
    }
    __builtin_trap();
}

void threads_test_infinite_sleep_fn(void* arg) {
    mx_nanosleep(UINT64_MAX);
    __builtin_trap();
}

void threads_test_infinite_wait_fn(void* arg) {
    mx_handle_t event = *(mx_handle_t*)arg;
    mx_object_wait_one(event, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL);
    __builtin_trap();
}

void threads_test_port_fn(void* arg) {
    mx_handle_t* port = (mx_handle_t*)arg;
    mx_port_packet_t packet = {};
    mx_port_wait(port[0], MX_TIME_INFINITE, &packet, 0u);
    packet.key += 5u;
    mx_port_queue(port[1], &packet, 0u);
}

void threads_test_channel_call_fn(void* arg_) {
    struct channel_call_suspend_test_arg* arg = arg_;

    uint8_t send_buf[9] = "abcdefghi";
    uint8_t recv_buf[9];
    uint32_t actual_bytes, actual_handles;

    mx_channel_call_args_t call_args = {
        .wr_bytes = send_buf,
        .wr_handles = NULL,
        .rd_bytes = recv_buf,
        .rd_handles = NULL,
        .wr_num_bytes = sizeof(send_buf),
        .wr_num_handles = 0,
        .rd_num_bytes = sizeof(recv_buf),
        .rd_num_handles = 0,
    };

    arg->read_status = MX_OK;
    arg->call_status = mx_channel_call(arg->channel, 0, MX_TIME_INFINITE, &call_args,
                                       &actual_bytes, &actual_handles, &arg->read_status);

    if (arg->call_status == MX_OK) {
        arg->read_status = MX_OK;
        if (actual_bytes != sizeof(recv_buf) || memcmp(recv_buf, "abcdefghj", sizeof(recv_buf))) {
            arg->call_status = MX_ERR_BAD_STATE;
        }
    }

    mx_handle_close(arg->channel);
}
