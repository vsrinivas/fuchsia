// Copyright 2017 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

void threads_test_sleep_fn(void* arg);
void threads_test_wait_fn(void* arg);
void threads_test_wait_detach_fn(void* arg);
void threads_test_busy_fn(void* arg);
void threads_test_infinite_sleep_fn(void* arg);
void threads_test_infinite_wait_fn(void* arg);
void threads_test_port_fn(void* arg);
void threads_test_channel_call_fn(void* arg);

struct channel_call_suspend_test_arg {
    mx_handle_t channel;
    mx_status_t call_status;
    mx_status_t read_status;
};
