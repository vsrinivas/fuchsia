// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

zx_time_t debuglog_next_timeout();

int debuglog_init();

void debuglog_recv(void* data, size_t len, bool is_mcast);

void debuglog_timeout_expired();


