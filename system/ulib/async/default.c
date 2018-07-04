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

// TODO(davemoore): ZX-2337 Remove after all external references have been changed
// to async_dispatcher_t.
async_t* async_get_default(void) {
    return async_get_default_dispatcher();
}

void async_set_default(async_t* async) {
    async_set_default_dispatcher(async);
}
