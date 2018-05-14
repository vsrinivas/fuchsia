// Copyright 2017 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

// This file contains thread functions that do various things useful for testing thread behavior.

// The arg is a zx_time_t which is passed to zx_nanosleep.
void threads_test_sleep_fn(void* arg);

// The arg is an event. It will first be waited on for signal 0, then it will issue signal 1 to
// notify completion.
void threads_test_wait_fn(void* arg);
void threads_test_wait_detach_fn(void* arg);

// The arg is an event which will be waited on for signal 0 (to synchronize the beginning), then
// it will issue a debug break instruction (causing a SW_BREAKPOINT exception), then it will sleep
// infinitely.
void threads_test_wait_break_infinite_sleep_fn(void* arg);

// This thread function busyloops forever. The arg is ignored.
void threads_test_busy_fn(void* arg);

// This thread function sleeps forever. The arg is ignored.
void threads_test_infinite_sleep_fn(void* arg);

// This thread issues an infinite wait on signal 0 of the event whose handle is passed in arg.
void threads_test_infinite_wait_fn(void* arg);

// The arg is a port handle which is waited on. When a packet is received, it will send a packet
// to the port whose key is 5 greater than the input key.
void threads_test_port_fn(void* arg);

// The arg is a pointer to channel_call_suspend_test_arg (below). The function will send a small
// message and expects to receive the same contents in a reply.
//
// On completion, arg->call_status will be set to the success of the operation.
void threads_test_channel_call_fn(void* arg);

struct channel_call_suspend_test_arg {
    zx_handle_t channel;
    zx_status_t call_status;
    zx_status_t read_status;
};
