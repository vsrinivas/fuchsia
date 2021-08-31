// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
#define SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_

#include <zircon/types.h>

zx_time_t debuglog_next_timeout();

zx_status_t debuglog_init();

void debuglog_recv(void* data, size_t len, bool is_mcast);

void debuglog_timeout_expired();

#endif  // SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
