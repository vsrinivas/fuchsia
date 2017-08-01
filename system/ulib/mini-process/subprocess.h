// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>

// This struct defines the first message that the child process gets.
typedef struct {
    __typeof(mx_handle_close)*      handle_close;
    __typeof(mx_object_wait_one)*   object_wait_one;
    __typeof(mx_object_signal)*     object_signal;
    __typeof(mx_event_create)*      event_create;
    __typeof(mx_channel_create)*    channel_create;
    __typeof(mx_channel_read)*      channel_read;
    __typeof(mx_channel_write)*     channel_write;
    __typeof(mx_process_exit)*      process_exit;
} minip_ctx_t;

// Subsequent messages and replies are of this format. The |what| parameter is
// transaction friendly so the client can use mx_channel_call().
typedef struct {
    mx_txid_t what;
    mx_status_t status;
} minip_cmd_t;

void minipr_thread_loop(mx_handle_t channel, uintptr_t fnptr);
