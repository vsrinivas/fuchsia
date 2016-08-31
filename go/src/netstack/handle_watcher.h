// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_HANDLE_WATCHER_H_
#define APPS_NETSTACK_HANDLE_WATCHER_H_

#define CTRL_COOKIE 0xffffffffffffffffull

#include <magenta/types.h>

#include "apps/netstack/iostate.h"

mx_status_t handle_watcher_start(void);
mx_status_t handle_watcher_stop(void);

mx_status_t handle_watcher_schedule_request(void);
;

mx_status_t handle_watcher_init(int* handle_watcher_fd);

void socket_signals_set(iostate_t* ios, mx_signals_t signals);
void socket_signals_clear(iostate_t* ios, mx_signals_t signals);

#endif  // APPS_NETSTACK_HANDLE_WATCHER_H_
