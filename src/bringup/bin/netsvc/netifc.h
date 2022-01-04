// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_H_

#include <lib/stdcompat/string_view.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Setup networking.
//
// If non-empty, `interface` holds the topological path of the interface
// intended to use for networking.
int netifc_open(cpp17::string_view interface);

// Process inbound packet(s).
int netifc_poll(zx_time_t deadline);

// Return nonzero if interface exists.
int netifc_active();

// Shut down networking.
void netifc_close();

void netifc_recv(void* data, size_t len);

// Send out next pending packet, and return value indicating if more are
// available to send.
bool netifc_send_pending();

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
