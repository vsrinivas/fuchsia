// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_H_

#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// setup networking
// if interface != NULL, only use the given topological path for networking
int netifc_open(const char* interface);

// process inbound packet(s)
int netifc_poll(zx_time_t deadline);

// return nonzero if interface exists
int netifc_active();

// shut down networking
void netifc_close();

void netifc_recv(void* data, size_t len);

// send out next pending packet, and return value indicating if more are available to send
bool netifc_send_pending();

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
