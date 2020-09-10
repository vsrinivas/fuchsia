// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls.h>

// This struct defines the first message that the child process gets.
typedef struct {
  __typeof(zx_handle_close)* handle_close;
  __typeof(zx_object_wait_async)* object_wait_async;
  __typeof(zx_object_wait_one)* object_wait_one;
  __typeof(zx_object_signal)* object_signal;
  __typeof(zx_event_create)* event_create;
  __typeof(zx_profile_create)* profile_create;
  __typeof(zx_channel_create)* channel_create;
  __typeof(zx_channel_read)* channel_read;
  __typeof(zx_channel_write)* channel_write;
  __typeof(zx_process_exit)* process_exit;
  __typeof(zx_object_get_info)* object_get_info;
  __typeof(zx_port_cancel)* port_cancel;
  __typeof(zx_port_create)* port_create;
  __typeof(zx_pager_create)* pager_create;
  __typeof(zx_pager_create_vmo)* pager_create_vmo;
  __typeof(zx_vmo_create_contiguous)* vmo_contiguous_create;
  __typeof(zx_vmo_create_physical)* vmo_physical_create;
  __typeof(zx_vmo_replace_as_executable)* vmo_replace_as_executable;
} minip_ctx_t;

// Subsequent messages and replies are of this format. The |what| parameter is
// transaction friendly so the client can use zx_channel_call().
typedef struct {
  zx_txid_t what;
  zx_status_t status;
} minip_cmd_t;

void minipr_thread_loop(zx_handle_t channel, uintptr_t fnptr);
