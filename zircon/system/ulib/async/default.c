// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>

#include <threads.h>

static thread_local async_dispatcher_t* g_default;

async_dispatcher_t* async_get_default_dispatcher(void) {
    return g_default;
}

void async_set_default_dispatcher(async_dispatcher_t* dispatcher) {
    g_default = dispatcher;
}
