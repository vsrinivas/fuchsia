// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_MULTIPLEXER_H_
#define APPS_NETSTACK_MULTIPLEXER_H_

#include <magenta/types.h>

int multiplexer(void* arg);

mx_status_t interrupter_create(int* sender, int* receiver);

mx_status_t send_interrupt(int sender);
mx_status_t clear_interrupt(int receiver);

#endif  // APPS_NETSTACK_MULTIPLEXER_H_
