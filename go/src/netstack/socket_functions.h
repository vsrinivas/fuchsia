// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_SOCKET_FUNCTIONS_H_
#define APPS_NETSTACK_SOCKET_FUNCTIONS_H_

#include <magenta/types.h>

#include "apps/netstack/iostate.h"
#include "apps/netstack/request_queue.h"

#define MXIO_SIGNAL_SOCKET_INCOMING_CONNECTION MX_USER_SIGNAL_0
#define MXIO_SIGNAL_SOCKET_OUTGOING_CONNECTION MX_USER_SIGNAL_1
#define MXIO_SIGNAL_SOCKET_ERROR MX_USER_SIGNAL_2

void handle_close(iostate_t* ios, mx_signals_t signals);
void put_rwbuf(rwbuf_t* bufp);
void handle_request(request_t* rq, int events, mx_signals_t signals);

#endif  // APPS_NETSTACK_SOCKET_FUNCTIONS_H_
