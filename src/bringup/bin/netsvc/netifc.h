// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/status.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Setup networking.
//
// If non-empty, `interface` holds the topological path of the interface
// intended to use for networking.
zx::status<> netifc_open(async_dispatcher_t* dispatcher, cpp17::string_view interface,
                         fit::callback<void(zx_status_t)> on_error);

// Return nonzero if interface exists.
int netifc_active();

// Shut down networking.
void netifc_close();

void netifc_recv(async_dispatcher_t* dispatcher, void* data, size_t len);

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
