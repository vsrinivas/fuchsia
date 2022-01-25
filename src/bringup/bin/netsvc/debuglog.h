// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
#define SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_

#include <lib/async/dispatcher.h>
#include <zircon/types.h>

zx_status_t debuglog_init(async_dispatcher_t* dispatcher);

void debuglog_recv(async_dispatcher_t* dispatcher, void* data, size_t len, bool is_mcast);

#endif  // SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
